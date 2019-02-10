/*
 * Filename: svc_mtxprt.h
 * Project: rpc-mt
 * Brief: Definition of multi-threaded extensions to @type{SVCXPRT}
 *
 * Copyright (C) 2016,2017 Guy Shaw
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

#ifndef _SVC_MTXPRT_H
#define _SVC_MTXPRT_H 1

#ifdef  __cplusplus
extern "C" {
#endif

#include <sys/types.h>   // Import caddr_t
#include <pthread.h>     // Import pthread_t, pthread_mutex_t
#include <rpc/xdr.h>     // Import XDR

#define XPRT_ID_INVALID ((size_t)(-1))
#define NO_PARENT ((size_t)(-1))

#define RQCRED_SIZE 400 /* This size is excessive */

// XXX #define XPRT_BUSY       0x01
#define XPRT_DONE_RECV  0x02
#define XPRT_DONE_READ  0x04

#define XPRT_GETARGS   0x010
#define XPRT_DISPATCH  0x020
#define XPRT_REPLY     0x040
#define XPRT_FREEARGS  0x080
#define XPRT_WAIT      0x100
#define XPRT_RETURN    0x200

/*
 * Extension to SVCXPRT structure with additional information and copies
 * of some data needed for thread safety.
 * Space is allocated at the end of the "public" @type{SVCXPRT} structure,
 * so that the extended @type{SVCXPRT} is backward compatible with exisiting
 * Linux / GNU libc code.
 *
 *
 * Fields of @type{mtxprt}
 * -----------------------
 *
 * mtxp_id:
 *     Unique ID number for each SVCXPRT structure.
 *     Used sort of like file descriptors, but no I/O or system call
 *     of any kind uses them.  They are just used to allocate and free
 *     @type{SVCXPRT} structures, and quickly index into an array
 *     containing pointers to all the active SVCXPRTs.
 *
 * mtxp_creator:
 *     The POSIX threads thread_id of the thread that created this
 *     SVCXPRT.
 *
 * mtxp_lock:
 *     pthread_mutex per SVCXPRT.
 *
 * mtxp_bufsz:
 *     Used to remember the bufsize of an "original" parent SVCXPRT,
 *     so that a clone can allocate a buffer of the same size.
 *
 * mtxp_parent:
 *     The ID number (mtxp_id) of my parent SVCXPRT.
 *     An original SVCXPRT records parent ID of -1.
 *
 * mtxp_clone:
 *     Pointer to the function that clones an SVCXPRT.
 *     The cloning process is slightly different for different transport types,
 *     such as "tcp" vs "udp".  UDP is more complicated to manage.
 *     TCP has its own form of cloning, so mtxp_clone is set to NULL.
 *     This would have been added to the method pointers in the original
 *     SVCXPRT, but we do not make any changes to that, because it is
 *     "published" in /usr/include and known by other functions in libc.
 *
 * The following three fields are kept in each SVCXPRT, so that the
 * pointer to the request (svc_req) that is handed to a worker thread
 * is a truly private copy, as is everything that is pointed to by
 * the @type{struct svc_req}.  The original single-threaded code
 * created the @type{struct svc_req}, @type{struct rpc_msg}, and
 * the credentials area on its stack, and passed a pointer to that.
 * That does not work for multi-threaded code.
 *
 * mtxp_rqst:
 *     @type{struct svc_req}.
 *     Information about the request, including program, version,
 *     and procedure.  The worker thread reads the procedure, in order
 *     to dispatch to the proper underlying function.  So, the
 *     @type{struct svc_req} must be made available in an MT_SAFE place.
 *
 * mtxp_msg:
 *     @type{struct rpc_msg}.
 *
 * mtxp_cred:
 *     @type{unstructured bytes}.
 *     Both @member{mtxp_rqst} and @member{mtxp_msg} point into the
 *     credentials area, so it needs to be private, per worker thread,
 *     as well.
 *
 */

#define MTXPRT_MAGIC 0x12345  // Dumb

#define MTXPRT_GUARD "MTXPRT_"

typedef SVCXPRT *(*clone_func_t)(SVCXPRT *);
typedef void (*update_func_t)(SVCXPRT *, SVCXPRT *);

struct mtxprt {
    int              mtxp_magic;
    size_t           mtxp_id;
    pthread_t        mtxp_creator;
    pthread_mutex_t  mtxp_lock;
    pthread_mutex_t  mtxp_mtready;
    pthread_mutex_t  mtxp_progress_lock;
    size_t           mtxp_bufsz;
    size_t           mtxp_parent;
    int              mtxp_refcnt;
    int              mtxp_fsck_refcnt;
    int              mtxp_busy;
    int              mtxp_pad;
    int              mtxp_progress;
    clone_func_t     mtxp_clone;
    int              mtxp_stat;
    struct svc_req   mtxp_rqst;
    struct rpc_msg   mtxp_msg;
    char *           mtxp_cred;
    char             mtxp_guard[8];
};
 
typedef struct mtxprt mtxprt_t;


/*
 * Simple functions to navigate from a pointer to @type{SVCXPRT}
 * to its MT-extensions.
 *
 * There are two functions, one that does some extra checking of
 * the validity of the given @type{SVCXPRT}, and one that does no
 * checking.  Most code would use just plain @function{xprt_to_mtxprt},
 * but the no-checking variation is used for functions that are
 * in the middle of constructing (or cloning) a @type{SVCXPRT}
 * or debugging functions that walk the data structures that account
 * for all registered @type{SVCXPRT}s and do so without locking,
 * and can tolerate walking over a @type{SVCXPRT} that is under
 * construction or is being destroyed.
 *
 */
extern mtxprt_t *xprt_to_mtxprt(SVCXPRT *xprt);
extern mtxprt_t *xprt_to_mtxprt_nocheck(SVCXPRT *xprt);


#ifdef  __cplusplus
}
#endif

#endif /* _SVC_MTXPRT_H */
