/*
 * Filename: decode.h
 * Project: libcscript
 * Brief: Interface for libcscript / libdecode
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

#ifndef _DECODE_H
#define _DECODE_H 1

#ifdef  __cplusplus
extern "C" {
#endif

#include <unistd.h>	// Import type size_t

/*
 * Top-level decode_*() functions
 */

extern char *decode_esym_r(char *buf, size_t bufsz, int e);
extern char *decode_poll_events_r(char *buf, size_t bufsz, int events);
extern char *decode_poll_events(int events);
extern char *decode_inet_family_r(char *buf, size_t bufsz, int family);
extern char *decode_inet_family(int family);
extern char *decode_inet_endpoint_r(char *buf, size_t bufsz, void *inet_addr);
extern char *decode_inet_endpoint(void *inet_addr);
extern char *decode_inet_peer_r(char *buf, size_t bufsz, int socket);
extern char *decode_inet_peer(int socket);
extern char *decode_addr_r(char *buf, size_t bufsz, void *addr);
extern char *decode_addr(void *addr);
extern char *decode_int_r(char *buf, size_t bufsz, int i);
extern char *decode_int(int i);
extern char *decode_xid_r(char *buf, size_t bufsz, size_t id, const char *neg1);
extern char *decode_xid(size_t id, const char *neg1);

#ifdef  __cplusplus
}
#endif

#endif /* _DECODE_H */
