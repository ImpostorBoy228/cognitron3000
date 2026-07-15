#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"

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

char* nostream(cJSON *root, CURL *curl) {
    struct Memory chunk = {NULL, 0};
    char *json_data = cJSON_Print(root);
    if (!json_data) return NULL;

    curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json_data));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    CURLcode res = curl_easy_perform(curl);
    free(json_data);

    if (res != CURLE_OK) {
        free(chunk.response);
        return NULL;
    }

    cJSON *resp_json = cJSON_Parse(chunk.response);
    free(chunk.response);
    if (!resp_json) return NULL;

    cJSON *choices = cJSON_GetObjectItem(resp_json, "choices");
    char *result = NULL;
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *first = cJSON_GetArrayItem(choices, 0);
        if (first) {
            cJSON *message = cJSON_GetObjectItem(first, "message");
            if (message) {
                cJSON *content = cJSON_GetObjectItem(message, "content");
                if (cJSON_IsString(content)) {
                    result = strdup(content->valuestring);
                }
            }
        }
    }
    cJSON_Delete(resp_json);
    return result;
}
