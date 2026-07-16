#ifndef KIOKU_H
#define KIOKU_H

#include <sqlite3.h>

typedef struct {
    sqlite3 *db;
    char name[16];
} DiaryDB;

static int dbinit(DiaryDB *d) { return sqlite3_open(d->name, &d->db); }
static void dbclose(DiaryDB *d) { sqlite3_close(d->db); }
static int dbreq(DiaryDB *d, const char *sql) { return sqlite3_exec(d->db, sql, 0, 0, NULL); }

#endif
