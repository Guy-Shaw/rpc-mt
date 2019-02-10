/*
 * Filename: syscall.c
 * Project: rpc-mt
 * Brief: Wrappers for various sytem call to make it easy to break using GDB
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

#include <stdint.h>
    // Import type uintptr_t
#include <stdio.h>
    // Import type FILE
    // Import fprintf()
    // Import fputc()
    // Import var stderr
#include <unistd.h>
    // Import type size_t
    // Import write()

#include "svc_debug.h"

extern unsigned int io_trace;
extern unsigned int sys_break;

void
gdb_syscall(void)
{
}

ssize_t
sys_read(int fd, void *buf, size_t count)
{
    ssize_t rsize;

    if (sys_break) {
        gdb_syscall();
    }

    rsize = read(fd, buf, count);

    if (io_trace) {
        tprintf(1, "read(fd=%d, buf=%p, %zu) => %zd\n",
            fd, buf, count, rsize);
        if (rsize > 0) {
            fprintf(stderr, "buf:\n");
            fhexdump(stderr, (rsize >= 16) ? 16: 0, 4, buf, rsize);
        }
    }

    return (rsize);
}

ssize_t
sys_write(int fd, const void *buf, size_t count)
{
    ssize_t rsize;

    if (sys_break) {
        gdb_syscall();
    }

    rsize = write(fd, buf, count);

    if (io_trace) {
        tprintf(1, "write(fd=%d, buf=%p, %zu) => %zd\n",
            fd, buf, count, rsize);
        fprintf(stderr, "buf:\n");
        if (rsize > 0) {
            fhexdump(stderr, (rsize >= 16) ? 16: 0, 4, buf, rsize);
        }
    }

    return (rsize);
}

int
sys_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    int rv;

    if (sys_break) {
        gdb_syscall();
    }

    rv = accept(sockfd, addr, addrlen);

    if (io_trace) {
        tprintf(1, "accept(sockfd=%d, addr=%p, %u) => %d\n",
            sockfd, addr, *addrlen, rv);
        fprintf(stderr, "addr=");
        fhexdump(stderr, 0, 4, addr, (size_t)(*addrlen));
    }
    return (rv);
}
