/*
 * Filename: decode-inet-ipv4addr.c
 * Project: libdecode
 * Library: libdecode
 * Brief: Decode an IPv4 address
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

#include <decode-impl.h>

#include <arpa/inet.h>    // inet_ntoa
#include <decode-impl.h>  // append_buf
#include <netinet/in.h>   // sockaddr_in
#include <stddef.h>       // NULL, size_t

struct sockaddr;

/*
 * Decode an IPv4 address.
 *
 * Never actually return NULL, no matter what might go wrong,
 * because this is a decode function, which should always return
 * something printable.
 */

char *
decode_inet_ipv4_addr_r(char *buf, size_t bufsz, struct sockaddr *addr)
{
    struct sockaddr_in *ipv4_addr;
    char *addr_str;

    addr_str = NULL;
    if (addr) {
        ipv4_addr = (struct sockaddr_in *)addr;
    }
    addr_str = inet_ntoa(ipv4_addr->sin_addr);
    if (addr_str == NULL) {
        addr_str = "<NULL>";
    }

    (void)append_buf(buf, bufsz, buf, addr_str);
    return (buf);
}
