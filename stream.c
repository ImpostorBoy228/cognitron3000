#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>

struct Memory {
    char *response;
    size_t size;
};

struct StreamState {
    char *buffer;
    size_t len;
    size_t cap;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    struct StreamState *s = (struct StreamState *)userp;

    size_t needed = s->len + total + 1;
    if (needed > s->cap) {
        size_t newcap = needed + 1024;
        char *ptr = realloc(s->buffer, newcap);
        if (!ptr) return 0;
        s->buffer = ptr;
        s->cap = newcap;
    }
    memcpy(s->buffer + s->len, contents, total);
    s->len += total;
    s->buffer[s->len] = '\0';

    char *p = s->buffer;
    char *nl;
    while ((nl = strchr(p, '\n')) != NULL) {
        *nl = '\0';
        char *line = p;
        size_t linelen = nl - p;
        if (linelen > 0 && line[linelen-1] == '\r')
            line[--linelen] = '\0';
        if (line[0] != '\0') {
            if (strncmp(line, "data: ", 6) == 0) {
                char *data = line + 6;
                if (strcmp(data, "[DONE]") != 0) {
                    json_object *json = json_tokener_parse(data);
                    if (json) {
                        json_object *choices;
                        if (json_object_object_get_ex(json, "choices", &choices)) {
                            if (json_object_is_type(choices, json_type_array) && json_object_array_length(choices) > 0) {
                                json_object *first = json_object_array_get_idx(choices, 0);
                                json_object *delta;
                                if (json_object_object_get_ex(first, "delta", &delta)) {
                                    json_object *content;
                                    if (json_object_object_get_ex(delta, "content", &content)) {
                                        printf("%s", json_object_get_string(content));
                                        fflush(stdout);
                                    }
                                }
                            }
                        }
                        json_object_put(json);
                    }
                }
            }
        }
        p = nl + 1;
    }

    s->len = s->len - (p - s->buffer);
    memmove(s->buffer, p, s->len);
    s->buffer[s->len] = '\0';

    return total;
}

static size_t write_memory(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    struct Memory *mem = (struct Memory *)userp;
    char *ptr = realloc(mem->response, mem->size + total + 1);
    if (!ptr) return 0;
    mem->response = ptr;
    memcpy(&(mem->response[mem->size]), contents, total);
    mem->size += total;
    mem->response[mem->size] = 0;
    return total;
}

char* readenv(void) {
    FILE *f = fopen(".env", "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, len, f);
    if (n != (size_t)len) { free(buf); fclose(f); return NULL; }
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    buf[n] = '\0';
    fclose(f);
    return buf;
}

void fuck(const char *reason) {
    fprintf(stderr, "fuck: %s\n", reason);
}

char* nostream(json_object *root, CURL *curl) {
    struct Memory chunk = {NULL, 0};
    const char *json_str = json_object_to_json_string(root);
    if (!json_str) return NULL;

    curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json_str));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        free(chunk.response);
        return NULL;
    }

    json_object *resp_json = json_tokener_parse(chunk.response);
    free(chunk.response);
    if (!resp_json) return NULL;

    json_object *choices;
    char *result = NULL;
    if (json_object_object_get_ex(resp_json, "choices", &choices)) {
        if (json_object_is_type(choices, json_type_array) && json_object_array_length(choices) > 0) {
            json_object *first = json_object_array_get_idx(choices, 0);
            json_object *message;
            if (json_object_object_get_ex(first, "message", &message)) {
                json_object *content;
                if (json_object_object_get_ex(message, "content", &content)) {
                    result = strdup(json_object_get_string(content));
                }
            }
        }
    }
    json_object_put(resp_json);
    return result;
}

int main() {
    int ret = 0;
    CURL *curl = NULL;
    CURLcode res;
    struct StreamState chunk = {NULL, 0, 0};
    json_object *root = NULL;
    char *json_data = NULL, *token = NULL;
    struct curl_slist *headers = NULL;

    root = json_object_new_object();
    json_object_object_add(root, "model", json_object_new_string("nvidia/nemotron-3-super-120b-a12b:free"));
    json_object_object_add(root, "stream", json_object_new_boolean(1));

    json_object *messages = json_object_new_array();
    json_object *msg = json_object_new_object();
    json_object_object_add(msg, "role", json_object_new_string("user"));
    json_object_object_add(msg, "content", json_object_new_string("hello. this is test for my c program. расскажи о коллбеках в c как будто ты демонический разраб с гитхаба. сделай это за 15 слов"));
    json_object_array_add(messages, msg);
    json_object_object_add(root, "messages", messages);

    json_object *reasoning = json_object_new_object();
    json_object_object_add(reasoning, "enabled", json_object_new_boolean(0));
    json_object_object_add(root, "reasoning", reasoning);

    json_data = strdup(json_object_to_json_string(root));
    if (!json_data) {
        fuck("to create JSON");
        ret = 1;
        goto naxyi;
    }

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    token = readenv();
    if (!token) {
        fuck("no .env file");
        ret = 1;
        goto naxyi;
    }
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth_header);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (!curl) {
        fuck("failed to init curl");
        ret = 1;
        goto naxyi;
    }

    curl_easy_setopt(curl, CURLOPT_URL, "https://openrouter.ai/api/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fuck("curl failed");
        ret = 1;
    }
    printf("\n");

    curl_easy_cleanup(curl);
    free(chunk.buffer);

    curl_global_cleanup();

naxyi:
    free(token);
    free(json_data);
    json_object_put(root);
    curl_slist_free_all(headers);
    return ret;
}
