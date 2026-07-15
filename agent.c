#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"

#include "nostream.h"

typedef struct msg {
	cJSON* msg;
	struct msg* prev;
	struct msg* next;
} msg;

typedef struct {
	msg* head;
	msg* tail;
	int count;
} messages;

static msg* msg_new(const char* role, const char* content) {
	msg* node = calloc(1, sizeof(msg));
	if (!node) return NULL;
	node->msg = cJSON_CreateObject();
	if (!node->msg) { free(node); return NULL; }
	cJSON_AddStringToObject(node->msg, "role", role);
	cJSON_AddStringToObject(node->msg, "content", content);
	return node;
}

static void append_msg(messages* list, msg* node) {
	if (!list->head) {
		list->head = list->tail = node;
	} else {
		list->tail->next = node;
		node->prev = list->tail;
		list->tail = node;
	}
	list->count++;
}

static cJSON* msgs_to_json(msg* head) {
	cJSON* arr = cJSON_CreateArray();
	if (!arr) return NULL;
	for (msg* cur = head; cur; cur = cur->next) {
		cJSON_AddItemToArray(arr, cJSON_Duplicate(cur->msg, 1));
	}
	return arr;
}

static void free_msgs(messages* list) {
	msg* cur = list->head;
	while (cur) {
		msg* next = cur->next;
		cJSON_Delete(cur->msg);
		free(cur);
		cur = next;
	}
	list->head = list->tail = NULL;
	list->count = 0;
}

static char* rfile(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	long len = ftell(f);
	if (len <= 0) { fclose(f); return NULL; }
	fseek(f, 0, SEEK_SET);
	char* buf = malloc(len + 1);
	if (!buf) { fclose(f); return NULL; }
	size_t n = fread(buf, 1, len, f);
	if (n != (size_t)len) { free(buf); fclose(f); return NULL; }
	fclose(f);
	buf[len] = '\0';
	return buf;
}

static int wfile(const char* path, const char* data) {
	FILE* f = fopen(path, "wb");
	if (!f) return -1;
	size_t len = strlen(data);
	size_t n = fwrite(data, 1, len, f);
	fclose(f);
	return n == len ? 0 : -1;
}

int main(void) {
    CURL *curl = NULL;
    cJSON *root = NULL;
    char *result = NULL, *token = NULL;
    struct curl_slist *headers = NULL;
    int ret = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "fuck: failed to init curl\n");
        curl_global_cleanup();
        return 1;
    }

    token = readenv();
    if (!token) {
        fprintf(stderr, "fuck: no .env file\n");
        goto naxyi;
    }

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    char auth[512];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth);

    curl_easy_setopt(curl, CURLOPT_URL, "https://openrouter.ai/api/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    messages chat = {NULL, NULL, 0};

    append_msg(&chat, msg_new("system", "you are a helpful assistant. answer concisely but with full detail when needed."));
    append_msg(&chat, msg_new("user", "how to implement linked list in nasm?"));

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "nvidia/nemotron-3-super-120b-a12b:free");
    cJSON_AddItemToObject(root, "messages", msgs_to_json(chat.head));

    result = nostream(root, curl);
    if (result) {
        append_msg(&chat, msg_new("assistant", result));
        printf("%s\n", result);
        free(result);
    } else {
        fprintf(stderr, "fuck: nostream returned NULL\n");
        ret = 1;
    }

    cJSON_Delete(root);
    root = NULL;
    free_msgs(&chat);

naxyi:
    free(token);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    curl_global_cleanup();
    return ret;
}
