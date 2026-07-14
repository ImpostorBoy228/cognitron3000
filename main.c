#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"

struct Memory {
    char *response;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
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

void fuck(char* reason, FILE* stream) {
    fprintf(stream, "fuck: %s\n", reason);
    return;
}

int main() {
    int ret = 0;
    CURL *curl = NULL;
    CURLcode res;
    struct Memory chunk = {NULL, 0};
    cJSON *root = NULL, *resp_json = NULL;
    char *json_data = NULL, *token = NULL;
    struct curl_slist *headers = NULL;

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "nvidia/nemotron-3-super-120b-a12b:free");

    cJSON *messages = cJSON_CreateArray();
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", "what is fuck is going on?");
    cJSON_AddItemToArray(messages, msg);
    cJSON_AddItemToObject(root, "messages", messages);

    cJSON *reasoning = cJSON_CreateObject();
    cJSON_AddBoolToObject(reasoning, "enabled", 1);
    cJSON_AddItemToObject(root, "reasoning", reasoning);

    json_data = cJSON_Print(root);
    if (!json_data) {
        fprintf(stderr, "fuck: to create JSON\n");
        ret = 1;
        goto naxyi;
    }

    headers = curl_slist_append(headers, "Content-Type: application/json");
    token = readenv();
    if (!token) {
        fuck("no .env file", stderr);
        ret = 1;
        goto naxyi;
    }
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth_header);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://openrouter.ai/api/v1/chat/completions");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            resp_json = cJSON_Parse(chunk.response);
            if (resp_json) {
                cJSON *choices = cJSON_GetObjectItem(resp_json, "choices");
                if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                    cJSON *first = cJSON_GetArrayItem(choices, 0);
                    cJSON *message = cJSON_GetObjectItem(first, "message");
                    cJSON *content = cJSON_GetObjectItem(message, "content");
                    if (cJSON_IsString(content)) {
                        printf("AI высрал:\n%s\n", content->valuestring);
                    } else {
                        fuck("no string", stderr);
                    }
                } else {
                    fuck("no choices", stderr);
                    printf("raw response:\n%s\n", chunk.response);
                }
            } else {
                printf("fuck: json parsing:\n%s\n", chunk.response);
            }
        } else {
            fprintf(stderr, "curl failed: %s\n", curl_easy_strerror(res));
            ret = 1;
        }

        curl_easy_cleanup(curl);
        free(chunk.response);
    }

    curl_global_cleanup();

naxyi:
    free(token);
    free(json_data);
    cJSON_Delete(resp_json);
    cJSON_Delete(root);
    curl_slist_free_all(headers);
    return ret;
}
