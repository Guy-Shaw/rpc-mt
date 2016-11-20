/*
 * Filename: decode-inet-family.c
 * Project: libdecode
 * Library: libdecode
 * Brief: Decode the inet family (int) of an Internet connection
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

static const char *inet_af_table[] = {
    /*    0 */ "AF_UNSPEC",
    /*    1 */ "AF_UNIX",
    /*    2 */ "AF_INET",
    /*    3 */ "AF_AX25",
    /*    4 */ "AF_IPX",
    /*    5 */ "AF_APPLETALK",
    /*    6 */ "AF_NETROM",
    /*    7 */ "AF_BRIDGE",
    /*    8 */ "AF_ATMPVC",
    /*    9 */ "AF_X25",
    /*   10 */ "AF_INET6",
    /*   11 */ "AF_ROSE",
    /*   12 */ NULL,
    /*   13 */ "AF_NETBEUI",
    /*   14 */ "AF_SECURITY",
    /*   15 */ "AF_KEY",
    /*   16 */ "AF_NETLINK",
    /*   17 */ "AF_PACKET",
    /*   18 */ "AF_ASH",
    /*   19 */ "AF_ECONET",
    /*   20 */ "AF_ATMSVC",
    /*   21 */ "AF_RDS",
    /*   22 */ "AF_SNA",
    /*   23 */ "AF_IRDA",
    /*   24 */ "AF_PPPOX",
    /*   25 */ "AF_WANPIPE",
    /*   26 */ "AF_LLC",
    /*   27 */ NULL,
    /*   28 */ NULL,
    /*   29 */ "AF_CAN",
    /*   30 */ "AF_TIPC",
    /*   31 */ "AF_BLUETOOTH",
    /*   32 */ "AF_IUCV",
    /*   33 */ "AF_RXRPC",
    /*   34 */ "AF_ISDN",
    /*   35 */ "AF_PHONET",
    /*   36 */ "AF_IEEE802154",
    /*   37 */ "AF_MAX"
};

#define ELEMENTS(array) (sizeof (array) / sizeof (inet_af_table[0]))

char *
decode_inet_family_r(char *buf, size_t bufsz, int family)
{
    char *ebuf;
    const char *str_family;
    size_t inet_af_table_size = ELEMENTS(inet_af_table);

    snprintf(buf, bufsz, "%u=", family);
    ebuf = strend(buf);
    if (family >= 0 && (size_t)family < inet_af_table_size) {
        str_family = inet_af_table[(size_t)family];
    }
    else {
        str_family = "?";
    }
    append_buf(buf, bufsz, ebuf, str_family);
    return (buf);
}

char *
decode_inet_family(int family)
{
    return (decode_inet_family_r(dbuf_thread_alloc(32), 32, family));
}
