/*
 * Filename: decode-size.c
 * Project: libdecode
 * Library: libdecode
 * Brief: Decode a size_t
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

#include <stdio.h>      // snprintf()

#include <decode-impl.h>

/*
 * Decode a size_t.
 * Always show %zu representation.
 * Also show representation as a small number with SI suffix,
 * if the address is easily represented that way.
 * That is, if the address can accurately be represented
 * as a small magnitude ( <= 9999 ) followed by an SI suffix.
 * The SI suffix really represents the nearby power of 2
 * computer engineering system, in which 1K is 1024,
 * and higher orders of magnitude {M, G, T, P, E} are
 * multiples of 1024, not 1000.
 *
 */

char *
decode_size_r(char *buf, size_t bufsz, size_t sz)
{
    snprintf(buf, bufsz, "%zu", sz);

    if (sz >= 1024) {
        size_t range;

        range = sz;
        while (range >= 1024 && (range & 1023) == 0)
            range /= 1024;
        if (range <= 9999) {
            while (*buf)
                ++buf;
            *buf++ = '=';
            /*
            * Since @var{sz} has been range reduced,
            * it will fit in a @type{uint32_t},
            * even if addesses are 64 bits.
            */
            (void) sisfx32_r(buf, (uint32_t)sz);
        }
    }

    return (buf);
}

char *
decode_size(size_t sz)
{
    return (decode_size_r(dbuf_thread_alloc(32), 32, sz));
}
