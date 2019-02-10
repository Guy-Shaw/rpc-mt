/*
 * Filename: decode-int.c
 * Project: libdecode
 * Library: libdecode
 * Brief: Decode the builtin C datatype, int.
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

/*
 * Even for something a simple to decode as a builtin datatype, such as 'int',
 * it pays to have both a reentrant version and a non-reentrant version that
 * goes along with the whole scheme of allocating and freeing "pseudo-static"
 * decode buffers.
 *
 */

#include <stdio.h>	// Import snprintf()

#include <decode-impl.h>

char *
decode_int_r(char *buf, size_t bufsz, int i)
{
    (void)snprintf(buf, bufsz, "%d", i);
    return (buf);
}

char *
decode_int(int i)
{
    return decode_int_r(dbuf_thread_alloc(32), 32, i);
}
