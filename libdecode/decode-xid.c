/*
 * Filename: decode-xid.c
 * Project: rpc-mt
 * Library: libdecode
 * Brief: Decode an xid, where xid is an index into an array of SVCXPRT *xprt
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
#include <string.h>     // strncpy()

#include <decode-impl.h>

/*
 * Decode an index into an array of SVCXPRT *xprt.
 * Such an index is of underlying type size_t,
 * but special values such as (size_t)(-1) are shown symboloically.
 * Always show %zu representation.
 *
 */

char *
decode_xid_r(char *buf, size_t bufsz, size_t id, const char *neg1)
{
    if (id == (size_t)(-1)) {
        strncpy(buf, neg1, bufsz);
    }
    else {
        snprintf(buf, bufsz, "%zu", id);
    }
    return (buf);
}

char *
decode_xid(size_t sz, const char *neg1)
{
    return (decode_xid_r(dbuf_thread_alloc(32), 32, sz, neg1));
}
