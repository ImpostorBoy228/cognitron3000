#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>

struct Memory {
    char *response;
    size_t size;
};

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

char* readenv(const char *path) {
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

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json_str));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);

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
