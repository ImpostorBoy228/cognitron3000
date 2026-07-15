#ifndef NOSTREAM_H
#define NOSTREAM_H

#include <curl/curl.h>
#include "cJSON.h"

char* nostream(cJSON *root, CURL *curl);
char* readenv(void);

#endif
