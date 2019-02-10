/*
 * Filename: util.c
 * Project: libdecode
 * Library: libdecode
 * Brief: Low-level support functions
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

#include <assert.h>
    // Import assert()
#include <unistd.h>
    // Import type size_t

#include <decode-impl.h>

/*
 * Utiliy functions used by many libdecode functions.
 */

char *
append_buf(char *buf, size_t bufsz, char *ebuf, char const *newstr)
{
    size_t cnt;

    assert(in_buf(buf, bufsz, ebuf));
    cnt = (buf + bufsz) - ebuf;
    while (cnt != 0 && *newstr != 0) {
        *ebuf++ = *newstr++;
        --cnt;
    }
    *ebuf = '\0';
    return (ebuf);
}

int
popcount_int(int i)
{
    unsigned int w;
    int bits;

    w = i;
    bits = 0;
    while (w != 0) {
        w &= (w - 1);
        ++bits;
    }
    return (bits);
}
