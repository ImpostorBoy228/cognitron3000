#ifndef NOSTREAM_H
#define NOSTREAM_H

#include <curl/curl.h>
#include <json-c/json.h>

char* nostream(json_object *root, CURL *curl);
void fuck(const char *reason);

#endif
