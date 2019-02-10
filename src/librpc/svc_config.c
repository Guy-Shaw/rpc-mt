/*
 * Filename: svc_config.c
 * Project: rpc-mt
 * Brief: Extension to support changing configuration wihtout recompiling
 *
 * Copyright (C) 2016--2018 Guy Shaw
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

#include <errno.h>
    // Import var EFAULT
    // Import var EINVAL
    // Import var ENOENT
#include <stddef.h>
    // Import constant NULL
#include <string.h>
    // Import memcmp()
    // Import strcmp()
    // Import strlen()
#include <unistd.h>
    // Import type size_t

#include "svc_config.h"
#include "svc_tcp_impl.h"

extern void svc_trace(unsigned int lvl);

/*
 * Extended scope declarations.
 *
 * The C programming language does not distinguish between:
 *   1) functions or data that is external because it is part of the
 *      ABI and is meant to be visible to the outside world, and
 *
 *   2) functions or data that need to be declared extern (not static)
 *      only because it needs to be visible to other modules
 *      (C source files) within a library that implements the API,
 *      but ought not to be visible outside the library.
 *
 * We annotate extern function and/or data with PUBLIC or LIBRARY.
 * This does not enforce anything, but can be used by a linker script
 * to hide symbols (at least in a shared library).
 *
 * PUBLIC:
 *   Part of the API.  Visible to the outside world.
 *
 * LIBRARY:
 *   Not static.  Visible to other librpc files, but not part of the API.
 *   That is, not meant to be visible to the outside world.
 *
 * UNUSED:
 *   static functions that we do not want to remove, at least not yet.
 *   The function, ref_unused(), keeps the compiler quiet when all
 *   warnings are turned on.
 *   It also acts as a quick visual way to keep track of unused functions.
 *
 */

#define PUBLIC
#define LIBRARY
#define UNUSED static


// Control allocation of socket file descriptors for svc_tcp.
//
struct fd_region socket_fd_region = { 64, 1023, -1 };

/*
 * 0: single-threaded
 *        do not clone SVCXPRT;
 *        wait for @var{worker_return}
 * 1: multi-threaded, partial
 *        clone SVCXPRT for each request;
 *        single-threaded setup and teardown;
 *        but muti-threaded dispatch;
 *        after setup, do not wait
 * 2: fully multi-threaded
 *        clone SVCXPRT for each request;
 *        multi-threaded setup, dispatch, and teardown
 *
 * There is no interface for changing the mode.
 * It is for purposes of debugging and regression testing.
 */
int mtmode = 1;

/*
 * A production system should simply return status,
 * whether or not there is an error; the caller can
 * decide what to do in the case of error.  But,
 * while the system is being developed, it is useful
 * to die immediately upon error, so that the debugger
 * can examine the state of affairs right when an error
 * is detected, with a minimum of destruction of evidence.
 *
 * failfast == 0 -- production mode; just return status.
 * failfast == 1 -- debug mode; die immediately on error.
 */

int failfast = 0;
int wait_method_tcp = WAIT_MUTEX;
int wait_method_udp = WAIT_MUTEX;
int wait_trace_interval = 5;

unsigned int io_trace;
unsigned int sys_break;

/*
 * Some "reasonable values for jiffy
 *
 *  const long jiffy = 1000000; // 1,000,000 nsec ==   1 msec
 *  const long jiffy =  100000; //   100,000 nsec == 100 usec
 *  const long jiffy =   10000; //    10,000 nsec ==  10 usec
 */

long jiffy = 1000000; // 1,000,000 nsec ==   1 msec

static int
bstr_equal(const char *bstr, size_t len, const char *zstr)
{
    size_t zlen = strlen(zstr);
    return (zlen == len && memcmp(bstr, zstr, len) == 0);
}

#if 0

static int
bstrcmp(const char *bstr, size_t len, const char *zstr)
{
    size_t zlen = strlen(zstr);
    size_t cmplen = (zlen < len) ? zlen : len;
    return (memcmp(bstr, zstr, cmplen));
}

#endif

int
svc_config_set_fd_range(const char *arg)
{
    int n;

    if (strcmp(arg, "none") == 0) {
        socket_fd_region.lo = 0;
        socket_fd_region.hi = 0;
        socket_fd_region.order = 0;
        return (0);
    }

    n = 0;
    while (*arg && *arg != ',' && *arg != '-') {
        if (*arg >= '0' && *arg <= '9') {
            n = (n * 10) + (*arg - '0');
        }
        else {
            return (EINVAL);
        }
        ++arg;
    }
    socket_fd_region.lo = n;
    if (*arg == '\0') {
        return (EINVAL);
    }

    ++arg;
    n = 0;
    while (*arg && *arg != ',' && *arg != '-') {
        if (*arg >= '0' && *arg <= '9') {
            n = (n * 10) + (*arg - '0');
        }
        else {
            return (EINVAL);
        }
        ++arg;
    }
    socket_fd_region.hi = n;
    if (*arg == '\0') {
        return (EINVAL);
    }

    ++arg;
    if (*arg == '-') {
        socket_fd_region.order = -1;
    }
    else if (*arg == '+') {
        socket_fd_region.order = 1;
    }
    else {
        return (EINVAL);
    }

    return (0);
}

static int
svc_config_set_mtmode(const char *arg)
{
    if (arg == NULL || arg[0] == '\0' || arg[1] != '\0') {
        return (EFAULT);
    }
    switch (arg[0]) {
        default:
            return (EINVAL);
        case '0':
        case '1':
        case '2':
            mtmode = arg[0] - '0';
    }

    return (0);
}

static int
svc_config_set_jiffy(const char *arg)
{
    long n;

    n = 0;
    while (*arg) {
        if (*arg >= '0' && *arg <= '9') {
            n = (n * 10) + (*arg - '0');
        }
        else {
            return (EINVAL);
        }
        ++arg;
    }

    jiffy = n;
    return (0);
}

static int
svc_config_set_trace(const char *arg)
{
    if (arg == NULL) {
        return (EFAULT);
    }
    else if (*arg >= '0' && *arg <= '9' && arg[1] == '\0') {
        svc_trace(*arg - '0');
        return (0);
    }
    else {
        return (EINVAL);
    }
}

static int
svc_config_lookup(const char *cmd, size_t len, const char *arg)
{
    if (bstr_equal(cmd, len, "fd-range")) {
        return (svc_config_set_fd_range(arg));
    }
    else if (bstr_equal(cmd, len, "mtmode")) {
        return (svc_config_set_mtmode(arg));
    }
    else if (bstr_equal(cmd, len, "failfast")) {
        failfast = 1;
        return (0);
    }
    else if (bstr_equal(cmd, len, "nofailfast")) {
        failfast = 0;
        return (0);
    }
    else if (bstr_equal(cmd, len, "jiffy")) {
        return (svc_config_set_jiffy(arg));
    }
    else if (bstr_equal(cmd, len, "trace")) {
        return (svc_config_set_trace(arg));
    }
    else if (bstr_equal(cmd, len, "io-trace")) {
        io_trace = 1;
        return (0);
    }
    else if (bstr_equal(cmd, len, "sys-break")) {
        sys_break = 1;
        return (0);
    }

    return (ENOENT);
}

PUBLIC int
svc_config(const char *cmd)
{
    size_t pos;

    pos = 0;
    for (;;) {
        if (cmd[pos] == '\0') {
            return (svc_config_lookup(cmd, pos, NULL));
        }
        else if (cmd[pos] == ' ') {
            return (svc_config_lookup(cmd, pos, NULL));
        }
        else if (cmd[pos] == '=') {
            return (svc_config_lookup(cmd, pos, cmd + pos + 1));
        }
        else {
            ++pos;
        }
    }
}
