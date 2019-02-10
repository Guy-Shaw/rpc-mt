/*
 * Filename: decode-time.c
 * Project: rpc-mt
 * Library: libdecode
 * Brief: Decode a date/time
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
    // Import snprintf()
#include <time.h>
    // Import localtime()
    // Import strftime()
    // Import type time_t
    // Import struct tm
#include <unistd.h>
    // Import type size_t

#include <decode-impl.h>

const char *
decode_time_r(char *buf, size_t sz, time_t t)
{
    struct tm *tmp;

    tmp = localtime(&t);
    if (tmp) {
        strftime(buf, sz, "%F %T", tmp);
    }
    else {
        snprintf(buf, sz, "*** ERROR ***  time=%zu", t);
    }
    return (buf);
}

const char *
decode_time(time_t t)
{
    char *buf = dbuf_thread_alloc(32);
    return (decode_time_r(buf, 32, t));
}
