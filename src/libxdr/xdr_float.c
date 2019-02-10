/*
 * xdr_float.c, Generic XDR routines implementation.
 *
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * These are the "floating point" xdr routines used to (de)serialize
 * most common data items.  See xdr.h for more info on the interface to
 * xdr.
 */

#include <stdio.h>
#include <endian.h>

#include <rpc/types.h>
#include <rpc/xdr.h>

#include <xdr_error.h>

/*
 * NB: Not portable.
 */

#define LSW	(__FLOAT_WORD_ORDER == __BIG_ENDIAN)

bool_t
xdr_float(XDR *xdrs, float *fp)
{
    switch (xdrs->x_op) {
    default:
        xdr_bad_op(__FILE__, __FUNCTION__, xdrs->x_op);
        break;

    case XDR_ENCODE:
        if (sizeof (float) == sizeof (long)) {
            return (XDR_PUTLONG(xdrs, (long *) fp));
        }
        else if (sizeof (float) == sizeof (int)) {
            long tmp = *(int *) fp;
            return (XDR_PUTLONG(xdrs, &tmp));
        }
        break;

    case XDR_DECODE:
        if (sizeof (float) == sizeof (long)) {
            return (XDR_GETLONG(xdrs, (long *) fp));
        }
        else if (sizeof (float) == sizeof (int)) {
            long tmp;
            if (XDR_GETLONG(xdrs, &tmp)) {
                *(int *) fp = tmp;
                return (TRUE);
            }
        }
        break;

    case XDR_FREE:
        return (TRUE);
    }
    return (FALSE);
}

bool_t
xdr_double(XDR * xdrs, double *dp)
{
    switch (xdrs->x_op) {
    default:
        xdr_bad_op(__FILE__, __FUNCTION__, xdrs->x_op);
        break;

    case XDR_ENCODE:
        if (2 * sizeof (long) == sizeof (double)) {
            long *lp = (long *) dp;
            return (XDR_PUTLONG(xdrs, lp + !LSW) && XDR_PUTLONG(xdrs, lp + LSW));
        }
        else if (2 * sizeof (int) == sizeof (double)) {
            int *ip = (int *) dp;
            long tmp[2];
            tmp[0] = ip[!LSW];
            tmp[1] = ip[LSW];
            return (XDR_PUTLONG(xdrs, tmp) && XDR_PUTLONG(xdrs, tmp + 1));
        }
        break;

    case XDR_DECODE:
        if (2 * sizeof (long) == sizeof (double)) {
            long *lp = (long *)dp;
            return (XDR_GETLONG(xdrs, lp + !LSW) && XDR_GETLONG(xdrs, lp + LSW));
        }
        else if (2 * sizeof (int) == sizeof (double)) {
            int *ip = (int *) dp;
            long tmp[2];
            if (XDR_GETLONG(xdrs, tmp + !LSW) && XDR_GETLONG(xdrs, tmp + LSW)) {
                ip[0] = tmp[0];
                ip[1] = tmp[1];
                return (TRUE);
            }
        }
        break;

    case XDR_FREE:
        return (TRUE);
    }
    return (FALSE);
}
