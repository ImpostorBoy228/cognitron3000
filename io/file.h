#ifndef FILE_H
#define FILE_H

static char* rfile(const char* path) {                                                FILE *f = fopen(path, "rb");                                                      if (!f) return NULL;                                                              if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }                       long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }                                         fseek(f, 0, SEEK_SET);
    char *buf = mi_malloc(len + 1);                                                   if (!buf) { fclose(f); return NULL; }                                             size_t n = fread(buf, 1, len, f);                                                 if (n != (size_t)len) { mi_free(buf); fclose(f); return NULL; }
    fclose(f);                                                                        buf[len] = '\0';                                                                  return buf;
}

static char* wfile(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");                                                      if (!f) return mi_strdup("{\"fuck\": \"cannot open file for writing\"}");
    size_t len = strlen(content);
    if (len > 0) {
        size_t n = fwrite(content, 1, len, f);
        if (n != len) { fclose(f); return mi_strdup("{\"fuck\": \"write failed\"}"); }
    }
    fclose(f);
    return NULL;
}

static char* readenv(const char *path) {                                        FILE *f = fopen(path, "rb");                                                if (!f) return NULL;                                                        if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }                 long len = ftell(f);                                                        if (len <= 0) { fclose(f); return NULL; }                                   fseek(f, 0, SEEK_SET);                                                      char *buf = mi_malloc(len + 1);                                             if (!buf) { fclose(f); return NULL; }                                       size_t n = fread(buf, 1, len, f);                                           if (n != (size_t)len) { mi_free(buf); fclose(f); return NULL; }             while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';    buf[n] = '\0';
    fclose(f);                                                                  return buf;                                                             }


#endif
