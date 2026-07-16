#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <telebot/telebot.h>

#include "nostream.h"

typedef struct msg {
    char *role;
    char *content;
    struct msg *prev;
    struct msg *next;
} msg;

typedef struct {
    msg *head;
    msg *tail;
    int count;
} messages;

static msg* msgnew(const char *role, const char *content) {
    msg *node = calloc(1, sizeof(msg));
    if (!node) return NULL;
    node->role = strdup(role);
    node->content = strdup(content);
    if (!node->role || !node->content) {
        free(node->role);
        free(node->content);
        free(node);
        return NULL;
    }
    return node;
}

static void msgappend(messages *list, msg *node) {
    if (!list->head) {
        list->head = list->tail = node;
    } else {
        list->tail->next = node;
        node->prev = list->tail;
        list->tail = node;
    }
    list->count++;
}

static json_object* msgs2json(msg *head) {
    json_object *arr = json_object_new_array();
    for (msg *cur = head; cur; cur = cur->next) {
        json_object *m = json_object_new_object();
        json_object_object_add(m, "role", json_object_new_string(cur->role));
        json_object_object_add(m, "content", json_object_new_string(cur->content));
        json_object_array_add(arr, m);
    }
    return arr;
}

static void msgsfree(messages *list) {
    msg *cur = list->head;
    while (cur) {
        msg *next = cur->next;
        free(cur->role);
        free(cur->content);
        free(cur);
        cur = next;
    }
    list->head = list->tail = NULL;
    list->count = 0;
}

static char* rfile(const char* path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, len, f);
    if (n != (size_t)len) { free(buf); fclose(f); return NULL; }
    fclose(f);
    buf[len] = '\0';
    return buf;
}

struct memchunk {
    char *buf;
    size_t len;
};

static size_t memwrite(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    struct memchunk *m = (struct memchunk *)userp;
    char *ptr = realloc(m->buf, m->len + total + 1);
    if (!ptr) return 0;
    m->buf = ptr;
    memcpy(m->buf + m->len, contents, total);
    m->len += total;
    m->buf[m->len] = 0;
    return total;
}

static char* llmreq(const char *user_text, CURL *curl) {
    char *apikey = readenv(".env");
    if (!apikey) return NULL;

    char *sys = rfile("prompts/system_main.md");
    if (!sys) { free(apikey); return NULL; }

    json_object *root = json_object_new_object();
    json_object_object_add(root, "model", json_object_new_string("gpt-oss-120b"));
    json_object_object_add(root, "max_completion_tokens", json_object_new_int(1024));
    json_object_object_add(root, "temperature", json_object_new_double(1.0));
    json_object_object_add(root, "top_p", json_object_new_int(1));
    json_object_object_add(root, "stream", json_object_new_boolean(0));
    json_object_object_add(root, "reasoning_effort", json_object_new_string("low"));

    json_object *msgs = json_object_new_array();
    json_object *sysmsg = json_object_new_object();
    json_object_object_add(sysmsg, "role", json_object_new_string("system"));
    json_object_object_add(sysmsg, "content", json_object_new_string(sys));
    json_object_array_add(msgs, sysmsg);
    free(sys);

    json_object *usermsg = json_object_new_object();
    json_object_object_add(usermsg, "role", json_object_new_string("user"));
    json_object_object_add(usermsg, "content", json_object_new_string(user_text));
    json_object_array_add(msgs, usermsg);

    json_object_object_add(root, "messages", msgs);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char auth[512];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", apikey);
    headers = curl_slist_append(headers, auth);
    free(apikey);

    const char *jstr = json_object_to_json_string(root);

    struct memchunk c = {NULL, 0};
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.cerebras.ai/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jstr);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, memwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&c);

    CURLcode res = curl_easy_perform(curl);
    json_object_put(root);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) { free(c.buf); return NULL; }

    json_object *jr = json_tokener_parse(c.buf);
    free(c.buf);
    if (!jr) return NULL;

    char *result = NULL;
    json_object *choices;
    if (json_object_object_get_ex(jr, "choices", &choices) && json_object_is_type(choices, json_type_array) && json_object_array_length(choices) > 0) {
        json_object *first = json_object_array_get_idx(choices, 0);
        json_object *msg;
        if (json_object_object_get_ex(first, "message", &msg)) {
            json_object *content;
            if (json_object_object_get_ex(msg, "content", &content)) {
                result = strdup(json_object_get_string(content));
            }
        }
    }

    json_object_put(jr);
    return result;
}

int main(void) {
    CURL *curl = NULL;
    telebot_handler_t bot;
    int ret = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        fuck("failed to init curl");
        curl_global_cleanup();
        return 1;
    }

    char *token = readenv(".token");
    if (!token) {
        fuck("no .token file with Telegram bot token");
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return 1;
    }

    if (telebot_create(&bot, token) != TELEBOT_ERROR_NONE) {
        fuck("telebot_create failed");
        free(token);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return 1;
    }
    free(token);

    telebot_user_t me;
    if (telebot_get_me(bot, &me) == TELEBOT_ERROR_NONE) {
        printf("bot: @%s (%lld)\n", me.username, me.id);
        telebot_put_me(&me);
    }

    int offset = 0;
    telebot_update_type_e types[] = {TELEBOT_UPDATE_TYPE_MESSAGE};

    while (1) {
        telebot_update_t *updates = NULL;
        int count = 0;

        telebot_error_e err = telebot_get_updates(bot, offset, 10, 10, types, 1, &updates, &count);
        if (err != TELEBOT_ERROR_NONE) {
            sleep(1);
            continue;
        }

        for (int i = 0; i < count; i++) {
            telebot_update_t *u = &updates[i];
            if (u->update_type != TELEBOT_UPDATE_TYPE_MESSAGE)
                continue;
            if (!u->message.text)
                continue;

            printf("msg from @%s: %s\n",
                u->message.from ? u->message.from->username : "?",
                u->message.text);

            telebot_send_chat_action(bot, u->message.chat->id, "typing");

            char *reply = llmreq(u->message.text, curl);
            if (reply) {
                telebot_error_e err = telebot_send_message(bot, u->message.chat->id, reply, "HTML", false, false, 0, NULL);
                if (err != TELEBOT_ERROR_NONE)
                    telebot_send_message(bot, u->message.chat->id, reply, NULL, false, false, 0, NULL);
                free(reply);
            } else {
                telebot_send_message(bot, u->message.chat->id,
                    "error: LLM request failed", NULL, false, false, 0, NULL);
            }

            offset = u->update_id + 1;
        }

        telebot_put_updates(updates, count);
    }

    telebot_destroy(bot);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return ret;
}
