/*
 * sqlite_mkfixture.c — build a tiny fixture .db for the sqlite_tui headless
 * smoke, using the vendored SQLite C API directly (no network, no CLI). Creates
 * a couple of tables with a few rows so `make smoke-sqlite-tui` can open it, run
 * a SELECT, and render a frame headlessly.
 *
 *   sqlite_mkfixture <db-path>
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#include "sqlite3.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    sqlite3 *db = NULL;
    char *err = NULL;
    const char *sql =
        "DROP TABLE IF EXISTS users;"
        "DROP TABLE IF EXISTS notes;"
        "CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, age INTEGER);"
        "INSERT INTO users(name,age) VALUES('alice',30),('bob',25),('carol',41);"
        "CREATE TABLE notes(id INTEGER PRIMARY KEY, user_id INTEGER, body TEXT);"
        "INSERT INTO notes(user_id,body) VALUES(1,'hello'),(2,'world');";

    if (argc < 2) {
        fprintf(stderr, "usage: %s <db-path>\n", argv[0]);
        return 2;
    }
    if (sqlite3_open(argv[1], &db) != SQLITE_OK) {
        fprintf(stderr, "sqlite_mkfixture: open %s: %s\n", argv[1], sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "sqlite_mkfixture: exec: %s\n", err ? err : "?");
        sqlite3_free(err);
        sqlite3_close(db);
        return 1;
    }
    sqlite3_close(db);
    return 0;
}
