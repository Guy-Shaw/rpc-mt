/*
 * Filename: decode-inet-endpoint.c
 * Project: libdecode
 * Brief: Decode an endpoint of an Internet connection (family, address, port)
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


#include <stdio.h>        // snprintf()
#include <arpa/inet.h>    // inet_ntop
#include <decode-impl.h>  // append_buf, strend, dbuf_thread_alloc
#include <errno.h>        // errno
#include <netinet/in.h>   // sockaddr_in
#include <stddef.h>       // NULL, size_t
#include <sys/socket.h>   // sockaddr
#include <sys/un.h>       // sa_family_t

#include <decode-impl.h>

char *
decode_inet_endpoint_r(char *buf, size_t bufsz, void *inet_addr)
{
    struct sockaddr_in *sockaddr;
    sa_family_t family;
    struct in_addr *ipaddr;
    int port;
    char *ebuf;
    const char *ntop_rv;
    int err;

    ebuf = buf;
    ebuf = append_buf(buf, bufsz, ebuf, "[");
    family = ((struct sockaddr *)inet_addr)->sa_family;
    decode_inet_family_r(ebuf, bufsz - (ebuf - buf), family);
    ebuf = strend(buf);
    ebuf = append_buf(buf, bufsz, ebuf, ":");
    sockaddr = (struct sockaddr_in *)inet_addr;
    ipaddr = &(sockaddr->sin_addr);
    ntop_rv = inet_ntop(family, ipaddr, ebuf, bufsz - (ebuf - buf));
    err = errno;
    if (ntop_rv != NULL) {
        ebuf = strend(ebuf);
    }
    else {
        ebuf = buf;
        *ebuf = 'E';
        ++ebuf;
        snprintf(ebuf, bufsz - (ebuf - buf), "%d", err);
        ebuf = strend(ebuf);
    }
    ebuf = append_buf(buf, bufsz, ebuf, ":");
    port = sockaddr->sin_port;
    snprintf(ebuf, bufsz - (ebuf - buf), "%d", port);
    ebuf = strend(ebuf);
    ebuf = append_buf(buf, bufsz, ebuf, "]");
    return (buf);
}

char *
decode_inet_endpoint(void *inet_addr)
{
    return (decode_inet_endpoint_r(dbuf_thread_alloc(64), 64, inet_addr));
}
