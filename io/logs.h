#ifndef LOGS_H
#define LOGS_H

#include <stdio.h>
#include <string.h>
#include "file.h"

void wlog(const char* entry) {
	FILE *f = fopen("logs/anton.log", "ab");
	if (!f) return;
	fwrite(entry, 1, strlen(entry), f);
	fwrite("\n", 1, 1, f);
	fclose(f);
}

#endif 
