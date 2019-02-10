/*
 * Filename: svc_tcp_impl.h
 * Project: rpc-mt
 * Brief: Definitions for mutli-threaded extensions to svc_tcp.c
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

#ifndef _SVC_TCP_IMPL_H
#define _SVC_TCP_IMPL_H 1

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * The allocation of file descriptors used for sockets can be controlled
 * by the fd_region data structure.  The values of @range{lo..hi} define
 * the region of numbers.  If both are zero, then file descriptors are used,
 * as they are issued by the operating system.  But, if there are positive
 * values for @range{lo..hi}, then new file descriptors for sockets are moved
 * to available slots inside the given range.
 *
 * order must be 1 or -1, for ascending or descending order.
 * Ascending order means that file descriptors are move to the lowest
 * number available within @range{lo..hi}, and descending (-1) means
 * move new file descriptors to the highest number available.
 *
 * XXX START ---------- OBSOLETE ----------
 * The reasons for restricting the range of file descriptors used by RPC
 * are mainly to get along with other software that might deal with more
 * than 1K file descriptors.
 *
 * This might change.  The use of FD_*() family of functions to keep
 * track of SVCXPRT structures is inherited from the Oracle code, and
 * is not necessary.  RPC-MT will probably convert to a better-behaved
 * data structure, soon, and as a result, the 1K limitation on the number
 * of SVCXPRT structures will go away.  But, there would still be a use
 * for restricting file descriptors to a range.
 * XXX END   ---------- OBSOLETE ----------
 *
 * Another consideration is that it can be difficult to maintain control
 * of all file descriptors involved in a multi-threaded service, because
 * the operating system has just the one policy for allocating file
 * descriptors: lowest available number.  This means that, if anything
 * goes wrong in a thread and a file is closed, then some other thread
 * that is supposed to be managing open files would not be aware of the
 * loss, immediately.  Meanwhile, another thread can open a file, and
 * quite likely get the same file descriptor as the one that just went
 * away.
 *
 * But, if we immediately move file dessriptors so that they are restricted
 * to a known range, and especially if we allocate them in descending order,
 * then we would not conflict with the operating system which always
 * allocates the next lowest available number.
 *
 */

struct fd_region {
    int lo;
    int hi;
    int order;
};

#ifdef  __cplusplus
}
#endif

#endif /* _SVC_TCP_IMPL_H */
