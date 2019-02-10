/*
 * Filename: bduf-thread.c
 * Project: libdecode
 * Library: libdecode
 * Brief: Manage per-thread "pseudo-static" decode buffers.
 *
 * Copyright (C) 2016 Guy Shaw
 * Written by Guy Shaw <gshaw@acm.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <pthread.h>    // pthread_getspecific, pthread_once, etc
#include <stddef.h>     // size_t
#include <stdio.h>      // NULL
#include <stdlib.h>     // free

#include <decode-impl.h>        // guard_malloc, guard_realloc

#define DBUF_INIT_SIZE 16

struct dbuf {
    char **dbufv;
    size_t sz;
    size_t len;
};

typedef struct dbuf dbuf_t;

static pthread_key_t  dbuf_key;
static pthread_once_t dbuf_key_once = PTHREAD_ONCE_INIT;

static void
dbuf_make_key(void)
{
    (void) pthread_key_create(&dbuf_key, NULL);
}

static void
dbuf_init(dbuf_t *dbuf)
{
    dbuf->dbufv = NULL;
    dbuf->sz  = 0;
    dbuf->len = 0;
}

static dbuf_t *
dbuf_new(void)
{
    dbuf_t *new_dbuf;
   
    new_dbuf = (dbuf_t *)guard_malloc(sizeof (dbuf_t));
    dbuf_init(new_dbuf);
    return (new_dbuf);
}

static void
dbuf_grow(dbuf_t *dbuf)
{
    char **new_dbufv;
    size_t new_dbuf_sz;
    size_t sz_bytes;

    if (dbuf->sz == 0) {
        new_dbuf_sz = DBUF_INIT_SIZE;
        sz_bytes = new_dbuf_sz * sizeof (char *);
        new_dbufv = (char **)guard_malloc(sz_bytes);
    }
    else {
        new_dbuf_sz = (dbuf->sz * 16) / 10;
        sz_bytes = new_dbuf_sz * sizeof (char *);
        new_dbufv = (char **)guard_realloc(dbuf->dbufv, sz_bytes);
    }
    dbuf->dbufv = new_dbufv;
    dbuf->sz = new_dbuf_sz;
}

char *
dbuf_thread_alloc(size_t bufsz)
{
    dbuf_t *dbuf;
    char *new_buf;

    (void) pthread_once(&dbuf_key_once, dbuf_make_key);
    dbuf = (dbuf_t *)pthread_getspecific(dbuf_key);
    if (dbuf == NULL) {
        dbuf = dbuf_new();
        (void) pthread_setspecific(dbuf_key, dbuf);
    }

    if (dbuf->len >= dbuf->sz) {
        dbuf_grow(dbuf);
    }

    new_buf = (char *)guard_malloc(bufsz);
    dbuf->dbufv[dbuf->len] = new_buf;
    ++dbuf->len;
    return (new_buf);
}

void
dbuf_thread_free_all(dbuf_t *dbuf)
{
    size_t nbufs;
    size_t bnr;

    nbufs = dbuf->len;
    for (bnr = 0; bnr < nbufs; ++bnr) {
        char *buf = dbuf->dbufv[bnr];
        if (buf != NULL) {
            free(buf);
            dbuf->dbufv[bnr] = NULL;
        }
    }
    dbuf->len = 0;
}

void
dbuf_thread_reset(void)
{
    dbuf_t *dbuf;

    (void) pthread_once(&dbuf_key_once, dbuf_make_key);
    dbuf = (dbuf_t *)pthread_getspecific(dbuf_key);
    if (dbuf == NULL) {
        return;
    }

    dbuf_thread_free_all(dbuf);
}

void
dbuf_thread_cleanup(void)
{
    dbuf_t *dbuf;

    dbuf_thread_reset();
    (void) pthread_once(&dbuf_key_once, dbuf_make_key);
    dbuf = (dbuf_t *)pthread_getspecific(dbuf_key);
    if (dbuf == NULL) {
        return;
    }
    if (dbuf->dbufv != NULL) {
        free(dbuf->dbufv);
        dbuf->dbufv = NULL;
        dbuf->sz = 0;
    }
}
