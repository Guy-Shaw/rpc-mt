/*
 * Filename: decode-xprt-stat.c
 * Project: rpc-mt
 * Library: libdecode
 * Brief: Decode a status of an SVCXPRT
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
#include <rpc/rpc.h>

#include <decode-impl.h>

/*
 * Decode the status of an SVCXPRT.
 *
 */

char *
decode_xprt_stat_r(char *buf, size_t bufsz, enum xprt_stat xrv)
{
    const char *xrv_str;

    switch (xrv) {
    case XPRT_DIED:
        xrv_str = "XPRT_DIED";
        break;
    case XPRT_MOREREQS:
        xrv_str = "XPRT_MOREREQS";
        break;
    case XPRT_IDLE:
        xrv_str = "XPRT_IDLE";
        break;
    default:
        snprintf(buf, bufsz, "XPRT-%u", (unsigned int) xrv);
        return (buf);
    }

    strcpy(buf, xrv_str);
    return (buf);
}

char *
decode_xprt_stat(enum xprt_stat xrv)
{
    return (decode_xprt_stat_r(dbuf_thread_alloc(16), 16, xrv));
}
