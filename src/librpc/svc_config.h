/*
 * Filename: svc_config.h
 * Project: rpc-mt
 * Brief: Definitions related to configurable values
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
 */

#ifndef _SVC_CONFIG_H
#define _SVC_CONFIG_H 1

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * When mtmode == 1, the server dispatch thread waits for svc_getargs(),
 * before accepting more connections.
 * 
 * There are two methods for waiting:
 *
 *   1) wait on a mutex contained in the SVCXPRT.
 *      That mutex starts of locked at the time of creation
 *      of the worker SVCXPRT.
 *      The worker thread must unlock it after svc_getargs() has completed.
 *
 *   2) WAIT_USLEEP
 *      sleeping in a loop, waiting for the worker thread to complete
 *      its call to svc_getargs() and set the milestone, XPRT_WAIT.
 *
 * Each transport type (TCP or UDP) is responsible for notifying
 * the main dispatch thread when svc_getargs() has completed,
 * because svc_getargs() resolves to svctcp_getargs() or svcudp_getargs().
 *
 * Which method to use is configurable.
 * The default is WAIT_MUTEX.
 *
 * It is possible to independently configure the wait method for
 * TCP and UDP connections.
 *
 * See svc.c  |wait_meth_tcp|  and  |wait_method_udp|.
 *
 */

#define WAIT_MUTEX  1
#define WAIT_USLEEP 2

#ifdef  __cplusplus
}
#endif

#endif /* _SVC_CONFIG_H */
