/*
 * Filename: svc_debug.c
 * Project: rpc-mt
 * Brief: Support for debugging / trace messages specific to svc
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>             // Import sleep()
#include <assert.h>

#include "svc_debug.h"

int opt_svc_trace = 0;

void
svc_trace(void)
{
    opt_svc_trace = 1;
}

void
svc_die()
{
    fflush(stdout);
    fflush(stderr);
    sleep(1);
    fflush(stdout);
    fflush(stderr);
    tprintf("\n");
    abort();
}
