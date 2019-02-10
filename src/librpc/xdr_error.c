/*
 * Filename: xdr_error.c
 * Library: glibc/sunrpc/xdr
 * Brief: Common error handling functions for sunrpc/xdr code
 *
 * Copyright (C) 2018 Guy Shaw
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
 *
 * This file contains error handling functions used by XDR routines.
 * The xdr*.c routines are normally found in GNU glibc/sunrpc, and
 * are embedded (imprisoned) in glibc and are difficult to modify
 * or debug or even observe.  The way of handling errors such as
 * out of memory is to print somewhere -- I do not know where.
 *
 * In order to more easily debug xdr code, the xdr*.c files have been
 * lifted and trascribed for use in a separate library, so that
 * they can be comiled with debugging and instrumentation in mind.
 * Ths file is provided so that the modified xdr*.c files can call
 * an extern error handling function.  That function could print to
 * standard error, then abort(), or do something else, in response
 * to errors such as out-of-memory, which usually is fatal..
 */

#include <stdio.h>
    // Import fprintf()
    // Import var stderr
#include <stdlib.h>
    // Import abort()

int xdr_failfast = 1;

void
xdr_abort(void)
{
    if (xdr_failfast) {
        abort();
    }
}

void
xdr_out_of_memory(const char *filename, const char *function)
{
    fprintf(stderr, "%s::%s: Out of memory\n",
        filename, function);
    xdr_abort();
}

void
xdr_bad_op(const char *filename, const char *function, int op)
{
    fprintf(stderr, "%s::%s: Invalid XDR operation, %d\n",
        filename, function, op);
    xdr_abort();
}

void
xdr_overflow(const char *filename, const char *function)
{
    fprintf(stderr, "%s::%s: Buffer overflow\n",
        filename, function);
    xdr_abort();
}
