/*
 * Filename: svc_util.c
 * Project: rpc-mt
 * Brief: Miscellaneous support functions common to librpc and libdecode
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>             // Import sleep()
#include <assert.h>

#include "svc_debug.h"

void *
guard_malloc(size_t size)
{
    void *mem;

    mem = malloc(size);
    if (mem == (void *)0) {
        teprintf("malloc(%zu) failed.\n", size);
        svc_die();
    }
    return (mem);
}

void *
guard_calloc(size_t nelem, size_t size)
{
    void *mem;

    mem = calloc(nelem, size);
    if (mem == (void *)0) {
        teprintf("calloc(%zu, %zu) failed.\n", nelem, size);
        svc_die();
    }
    return (mem);
}

void *
guard_realloc(void *old_mem, size_t size)
{
    void *new_mem;

    new_mem = realloc(old_mem, size);
    if (new_mem == (void *)0) {
        teprintf("realloc(size=%zu) failed.\n", size);
        svc_die();
    }
    return (new_mem);
}
