#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <telebot/telebot.h>

#include "記憶.h"
#include "nostream.h"
#include "libs/mimalloc.h"

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
    msg *node = mi_calloc(1, sizeof(msg));
    if (!node) return NULL;
    node->role = mi_strdup(role);
    node->content = mi_strdup(content);
    if (!node->role || !node->content) {
        mi_free(node->role);
        mi_free(node->content);
        mi_free(node);
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

static void msgsmi_free(messages *list) {
    msg *cur = list->head;
    while (cur) {
        msg *next = cur->next;
        mi_free(cur->role);
        mi_free(cur->content);
        mi_free(cur);
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
    char *buf = mi_malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, len, f);
    if (n != (size_t)len) { mi_free(buf); fclose(f); return NULL; }
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
    char *ptr = mi_realloc(m->buf, m->len + total + 1);
    if (!ptr) return 0;
    m->buf = ptr;
    memcpy(m->buf + m->len, contents, total);
    m->len += total;
    m->buf[m->len] = 0;
    return total;
}

static json_object* toolcall(json_object *msgs, CURL *curl) {
    char *apikey = readenv(".env");
    if (!apikey) return NULL;

    json_object *root = json_object_new_object();
    json_object_object_add(root, "model", json_object_new_string("gpt-oss-120b"));
    json_object_object_add(root, "max_completion_tokens", json_object_new_int(1024));
    json_object_object_add(root, "temperature", json_object_new_double(1.0));
    json_object_object_add(root, "top_p", json_object_new_int(1));
    json_object_object_add(root, "stream", json_object_new_boolean(0));
    json_object_object_add(root, "reasoning_effort", json_object_new_string("low"));
    json_object_object_add(root, "messages", json_object_get(msgs));

    json_object *tools = json_object_new_array();

    json_object *os_cmd = json_object_new_object();
    json_object_object_add(os_cmd, "type", json_object_new_string("function"));
    json_object *os_func = json_object_new_object();
    json_object_object_add(os_func, "name", json_object_new_string("os_command"));
    json_object_object_add(os_func, "description", json_object_new_string("Execute a bash command"));
    json_object *os_params = json_object_new_object();
    json_object_object_add(os_params, "type", json_object_new_string("object"));
    json_object *os_props = json_object_new_object();
    json_object *os_cmd_prop = json_object_new_object();
    json_object_object_add(os_cmd_prop, "type", json_object_new_string("string"));
    json_object_object_add(os_cmd_prop, "description", json_object_new_string("Command to execute"));
    json_object_object_add(os_props, "command", os_cmd_prop);
    json_object_object_add(os_params, "properties", os_props);
    json_object_object_add(os_params, "required", json_object_new_array_ext(0));
    json_object_object_add(os_func, "parameters", os_params);
    json_object_object_add(os_cmd, "function", os_func);
    json_object_array_add(tools, os_cmd);

    json_object *file_read = json_object_new_object();
    json_object_object_add(file_read, "type", json_object_new_string("function"));
    json_object *fr_func = json_object_new_object();
    json_object_object_add(fr_func, "name", json_object_new_string("file_read"));
    json_object_object_add(fr_func, "description", json_object_new_string("Read a file"));
    json_object *fr_params = json_object_new_object();
    json_object_object_add(fr_params, "type", json_object_new_string("object"));
    json_object *fr_props = json_object_new_object();
    json_object *fr_path_prop = json_object_new_object();
    json_object_object_add(fr_path_prop, "type", json_object_new_string("string"));
    json_object_object_add(fr_path_prop, "description", json_object_new_string("File path"));
    json_object_object_add(fr_props, "path", fr_path_prop);
    json_object_object_add(fr_params, "properties", fr_props);
    json_object_object_add(fr_params, "required", json_object_new_array_ext(0));
    json_object_object_add(fr_func, "parameters", fr_params);
    json_object_object_add(file_read, "function", fr_func);
    json_object_array_add(tools, file_read);

    json_object *file_write = json_object_new_object();
    json_object_object_add(file_write, "type", json_object_new_string("function"));
    json_object *fw_func = json_object_new_object();
    json_object_object_add(fw_func, "name", json_object_new_string("file_write"));
    json_object_object_add(fw_func, "description", json_object_new_string("Write content to a file"));
    json_object *fw_params = json_object_new_object();
    json_object_object_add(fw_params, "type", json_object_new_string("object"));
    json_object *fw_props = json_object_new_object();
    json_object *fw_path_prop = json_object_new_object();
    json_object_object_add(fw_path_prop, "type", json_object_new_string("string"));
    json_object_object_add(fw_path_prop, "description", json_object_new_string("File path"));
    json_object_object_add(fw_props, "path", fw_path_prop);
    json_object *fw_content_prop = json_object_new_object();
    json_object_object_add(fw_content_prop, "type", json_object_new_string("string"));
    json_object_object_add(fw_content_prop, "description", json_object_new_string("Content to write"));
    json_object_object_add(fw_props, "content", fw_content_prop);
    json_object_object_add(fw_params, "properties", fw_props);
    json_object_object_add(fw_params, "required", json_object_new_array_ext(0));
    json_object_object_add(fw_func, "parameters", fw_params);
    json_object_object_add(file_write, "function", fw_func);
    json_object_array_add(tools, file_write);

    json_object_object_add(root, "tools", tools);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char auth[512];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", apikey);
    headers = curl_slist_append(headers, auth);
    mi_free(apikey);

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

    if (res != CURLE_OK) { mi_free(c.buf); return NULL; }

    json_object *jr = json_tokener_parse(c.buf);
    mi_free(c.buf);
    return jr;
}

static char* exec_tool(const char *name, const char *args_json) {
    json_object *args = json_tokener_parse(args_json);
    if (!args) return mi_strdup("{\"error\": \"failed to parse args\"}");

    if (strcmp(name, "os_command") == 0) {
        json_object *cmd;
        if (!json_object_object_get_ex(args, "command", &cmd)) { json_object_put(args); return mi_strdup("{\"error\": \"missing command\"}"); }
        const char *command = json_object_get_string(cmd);
        FILE *fp = popen(command, "r");
        if (!fp) { json_object_put(args); return mi_strdup("{\"error\": \"popen failed\"}"); }
        char buf[4096];
        size_t len = 0;
        while (fgets(buf + len, sizeof(buf) - len, fp)) len = strlen(buf);
        pclose(fp);
        json_object_put(args);
        return mi_strdup(buf);

    } else if (strcmp(name, "file_read") == 0) {
        json_object *path;
        if (!json_object_object_get_ex(args, "path", &path)) { json_object_put(args); return mi_strdup("{\"error\": \"missing path\"}"); }
        char *content = rfile(json_object_get_string(path));
        json_object_put(args);
        return content ? content : mi_strdup("{\"error\": \"file not found\"}");

    } else if (strcmp(name, "file_write") == 0) {
        json_object *path, *content;
        if (!json_object_object_get_ex(args, "path", &path) || !json_object_object_get_ex(args, "content", &content)) { json_object_put(args); return mi_strdup("{\"error\": \"missing path or content\"}"); }
        FILE *f = fopen(json_object_get_string(path), "w");
        if (!f) { json_object_put(args); return mi_strdup("{\"error\": \"failed to open file for writing\"}"); }
        fwrite(json_object_get_string(content), 1, strlen(json_object_get_string(content)), f);
        fclose(f);
        json_object_put(args);
        return mi_strdup("{\"status\": \"written\"}");

    } else {
        json_object_put(args);
        char err[128];
        snprintf(err, sizeof(err), "{\"error\": \"unknown tool: %s\"}", name);
        return mi_strdup(err);
    }
}

static char* llmreq(const char *user_text, CURL *curl) {
    char *sys = rfile("prompts/system_main.md");
    if (!sys) return NULL;

    json_object *msgs = json_object_new_array();
    json_object *sysmsg = json_object_new_object();
    json_object_object_add(sysmsg, "role", json_object_new_string("system"));
    json_object_object_add(sysmsg, "content", json_object_new_string(sys));
    json_object_array_add(msgs, sysmsg);
    mi_free(sys);

    json_object *usermsg = json_object_new_object();
    json_object_object_add(usermsg, "role", json_object_new_string("user"));
    json_object_object_add(usermsg, "content", json_object_new_string(user_text));
    json_object_array_add(msgs, usermsg);

    char *result = NULL;
    int max_turns = 10;

    for (int turn = 0; turn < max_turns; turn++) {
        json_object *jr = toolcall(msgs, curl);
        if (!jr) break;

        json_object *choices;
        if (!json_object_object_get_ex(jr, "choices", &choices) || !json_object_is_type(choices, json_type_array) || json_object_array_length(choices) == 0) {
            json_object_put(jr);
            break;
        }

        json_object *first = json_object_array_get_idx(choices, 0);
        json_object *msg;
        if (!json_object_object_get_ex(first, "message", &msg)) { json_object_put(jr); break; }

        json_object *content;
        json_object *tool_calls;

        int has_tc = json_object_object_get_ex(msg, "tool_calls", &tool_calls)
                     && json_object_is_type(tool_calls, json_type_array)
                     && json_object_array_length(tool_calls) > 0;

        json_object *assistant_msg = json_object_new_object();
        json_object_object_add(assistant_msg, "role", json_object_new_string("assistant"));
        json_object_object_add(assistant_msg, "content", json_object_new_string(
            json_object_object_get_ex(msg, "content", &content) ? json_object_get_string(content) : ""));

        if (has_tc) {
            json_object *tc_copy = json_object_new_array();
            int n_tc = json_object_array_length(tool_calls);
            for (int i = 0; i < n_tc; i++) {
                json_object *tc = json_object_array_get_idx(tool_calls, i);
                json_object_array_add(tc_copy, json_object_get(tc));
            }
            json_object_object_add(assistant_msg, "tool_calls", tc_copy);
        }

        json_object_array_add(msgs, assistant_msg);

        if (!has_tc) {
            result = mi_strdup(json_object_get_string(content));
            json_object_put(jr);
            break;
        }

        int n_tc = json_object_array_length(tool_calls);
        for (int i = 0; i < n_tc; i++) {
            json_object *tc = json_object_array_get_idx(tool_calls, i);
            json_object *func;
            if (!json_object_object_get_ex(tc, "function", &func)) continue;
            json_object *tc_id_o;
            const char *tc_id = json_object_object_get_ex(tc, "id", &tc_id_o) ? json_object_get_string(tc_id_o) : "";
            const char *fname = json_object_get_string(json_object_object_get(func, "name"));
            const char *fargs = json_object_get_string(json_object_object_get(func, "arguments"));

            char *fresult = exec_tool(fname, fargs);

            json_object *tool_msg = json_object_new_object();
            json_object_object_add(tool_msg, "role", json_object_new_string("tool"));
            json_object_object_add(tool_msg, "tool_call_id", json_object_new_string(tc_id));
            json_object_object_add(tool_msg, "content", json_object_new_string(fresult));
            json_object_array_add(msgs, tool_msg);
            mi_free(fresult);
        }

        json_object_put(jr);
    }

    json_object_put(msgs);
    return result;
}

int main(void) {
    CURL *curl = NULL;
    telebot_handler_t bot;
    DiaryDB diary = {NULL, "diary.db"};
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
        mi_free(token);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return 1;
    }
    mi_free(token);

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
                mi_free(reply);
            } else {
                telebot_send_message(bot, u->message.chat->id,
                    "fuck: LLM request failed", NULL, false, false, 0, NULL);
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
