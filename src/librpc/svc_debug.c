/*
 * Filename: svc_debug.c
 * Project: rpc-mt
 * Brief: Support for debugging / trace messages specific to svc
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


#include <ctype.h>
    // Import isprint()
#include <stdbool.h>
    // Import type bool
#include <stdint.h>
    // Import type uintptr_t
#include <stdio.h>
    // Import type FILE
    // Import fflush()
    // Import fprintf()
    // Import fputc()
    // Import var stderr
    // Import var stdout
#include <stdlib.h>
    // Import abort()
#include <string.h>
    // Import memcmp()
#include <unistd.h>
    // Import type size_t
    // Import sleep()

#if 0
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>             // Import sleep()
#include <assert.h>
#include <ctype.h>
#endif

#include "svc_debug.h"

extern void xprt_destroy_all(void);
extern void destroy_xports(void);
extern void svc_run_cleanup();
extern void dbuf_thread_reset(void);
extern void dbuf_thread_cleanup(void);

int svc_quit = 0;

// Trace level from 0..9

unsigned int opt_svc_trace = 0;
unsigned int io_trace  = 0;
unsigned int sys_break = 0;

void
svc_trace(unsigned int lvl)
{
    opt_svc_trace = lvl;
}


void
svc_trace_flush(void)
{
    fflush(stdout);
    fflush(stderr);
    sleep(1);
    fputc('\n', stdout);
    fputc('\n', stderr);
    fflush(stdout);
    fflush(stderr);
}

/*
 * Memory manager for top-level allocations.
 * Most objects are responsible for clean constructors and destructors,
 * especially complex objects.  But at the top level, there are a few
 * arrays of plain old data that manage the rest.  It can be considered
 * the root the pool of objects in a poor-man's garbage collector.
 *
 * svc_l1_alloc() adds to the list of top-level objects.
 *
 * When svc_run() is shutdown, it is expected that svc_l1_cleanup()
 * will leave the service completely clean, so that, even if a long-running
 * program were to start svc_run(), then shut it down and go about some
 * other business, there would be no leftover memory allocations.
 *
 * Note: if we want to act like a real library, we would keep track
 * of top-level objects be means of a svc_context object that is created
 * and passed in as an argument svc_run().  Then, theoretically, there
 * could be more than one instance of svc_run() in a process, provided
 * some resource management of file descriptors, etc. is done cooperatively.
 *
 */

#define L1_INIT_SIZE 64
static void **l1_vec = NULL;
static size_t l1_size;
static size_t l1_len;

void *
svc_l1_alloc(size_t sz)
{
    if (l1_vec == NULL) {
        l1_vec = (void **) guard_malloc(sizeof (void *) * L1_INIT_SIZE);
        l1_size = L1_INIT_SIZE;
        l1_len = 0;
    }

    if (l1_len >= l1_size) {
        size_t new_l1_size = (l1_size * 16) / 10;
        l1_vec = (void **) guard_realloc(l1_vec, sizeof (void *) * new_l1_size);
    }
    void *p = guard_malloc(sz);
    l1_vec[l1_len] = p;
    ++l1_len;
    return (p);
}

void
svc_l1_cleanup(void)
{
    size_t i;

    if (l1_vec == NULL) {
        return;
    }

    for (i = 0; i < l1_len; ++i) {
        void *p = l1_vec[i];
        if (p != NULL) {
            free(p);
            l1_vec[i] = NULL;
        }
    }
    free(l1_vec);
}

void
svc_shutdown(void)
{
    svc_quit = 1;
    xprt_destroy_all();
    destroy_xports();
    // Free any memory managed by libdecode functions.
    dbuf_thread_reset();
    dbuf_thread_cleanup();
    svc_run_cleanup();
    svc_l1_cleanup();
    svc_trace_flush();
}

void
svc_die(void)
{
    svc_shutdown();
    abort();
}

void
uftrace_start(void)
{
}

void
uftrace_end(void)
{
}

bool
mem_is_zero(const void *mem, size_t sz)
{
    const char *mp = (const char *) mem;
    size_t len = sz;

    while (len != 0) {
        if (*mp != 0) {
            return (false);
        }
        ++mp;
        --len;
    }
    return (true);
}

void
fput_indent(FILE *f, size_t indent)
{
    size_t len;

    for (len = indent; len != 0; --len) {
        fputc(' ', f);
    }
}

void
fshowprint(FILE *f, const void *buf, size_t count)
{
    const char *bp = (char *)buf;
    int chr;

    while (count != 0) {
        chr = *bp;
        if (!isprint(chr)) {
            chr = '.';
        }
        fputc(chr, f);
        ++bp;
        --count;
    }
}

void
fhexdump_part(FILE *f, const void *buf, size_t count)
{
    char *bufc = (char *)buf;
    size_t pos;

    pos = 0;
    while (count != 0) {
        fprintf(f, "%02X", bufc[pos] & 0xff);
        ++pos;
        --count;
    }
}

void
fhexdump_row(FILE *f, size_t indent, size_t offset, const void *buf, size_t count)
{
    size_t len;

    fput_indent(f, indent);
    fprintf(f, "%12zx: ", offset);
    if (count > 16) {
        count = 16;
    }
    fhexdump_part(f, buf, count);
    if (count < 16) {
        for (len = 16 - count; len != 0; --len) {
            fputc(' ', f);
            fputc(' ', f);
        }
    }

    fputc(' ', f);
    fputc('|', f);
    fputc(' ', f);
    fshowprint(f, buf, count);
    fputc('\n', f);
}

void
fhexdump(FILE *f, size_t align, size_t indent, const void *buf, size_t count)
{
    const char *s = (char *) buf;
    size_t offset = 0;
    size_t phase;
    size_t zrunlen = 0;
    bool all_zero;
    

    phase = (align != 0) ? ((size_t) (uintptr_t) s) & (align - 1) : 0;

    if (phase != 0) {
        size_t len;

        fput_indent(f, indent);
        fprintf(f, "%12s: ", "");
        for (len = phase; len != 0; --len) {
            fputc('_', f);
            fputc('_', f);
        }
        offset = align - phase;
        fhexdump_part(f, buf, offset);
        s += offset;
        count -= offset;
        fputc('\n', f);
    }

    while (count != 0) {
        all_zero = count >= 16 && mem_is_zero((const void *) s, 16);
        if (all_zero) {
            ++zrunlen;
        }
        if (!all_zero && zrunlen >= 2) {
            fput_indent(f, indent);
            fprintf(f, "%12s  ...\n", "");
        }
        if (!all_zero || zrunlen == 1) {
            fhexdump_row(f, indent, offset, (const void *) s, count);
        }
        if (!all_zero) {
            zrunlen = 0;
        }
        s += 16;
        offset += 16;
        if (count >= 16) {
            count -= 16;
        }
        else {
            count = 0;
        }
    }
}
