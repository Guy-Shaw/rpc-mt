/*
 * Filename: xdr_debug.h
 * Project: rpc-mt
 * Brief: Debug/trace helper functions specific to XDR
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

#ifndef _XDR_DEBUG_H
#define _XDR_DEBUG_H 1

#ifdef  __cplusplus
extern "C" {
#endif


// extern pthread_mutex_t trace_lock;


extern int xdr_failfast;

extern void xdr_bad_op(const char *filename, const char *function, int op);
extern void xdr_out_of_memory(const char *filename, const char *function);

#ifdef  __cplusplus
}
#endif

#endif /* _XDR_DEBUG_H */
