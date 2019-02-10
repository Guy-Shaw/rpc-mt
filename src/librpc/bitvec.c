/*
 * Filename: bitvec.c
 * Project: rpc-mt
 * Library: libcscript
 * Brief: bit vector implementation - just enough to replace fd_set functions
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

#include "bitvec.h"

#include <stdlib.h>
    // Import free()

#include <limits.h>
    // Import macro CHAR_BIT

extern void *guard_malloc(size_t sz);
extern void svc_die(void);

#define BIT_SIZE(T) (CHAR_BIT * sizeof (T))

// bitmask to restrict the value of a shift within a single |bvword_t|
//
const size_t bvword_mask = sizeof (bvword_t) - 1;

void
bitvec_init(bitvec_t *bv, size_t nbits)
{
    size_t wsize;
    size_t i;
   
    wsize = (nbits + BIT_SIZE(bvword_t) - 1) / BIT_SIZE(bvword_t);
	bv->vec = (bvword_t *) guard_malloc(wsize * sizeof (bvword_t));
	bv->sz  = nbits;
    for (i = 0; i < wsize; ++i) {
        bv->vec[i] = 0;
    }
}

void
bitvec_free(bitvec_t *bv)
{
    if (bv->vec != NULL) {
	    free(bv->vec);
    }
}

void
bitvec_set_bit(bitvec_t *bv, size_t idx)
{
    size_t wpos;
    size_t bpos;

    if (idx >= bv->sz) {
        svc_die();
    }
    wpos = (idx + BIT_SIZE(bvword_t) - 1) / BIT_SIZE(bvword_t);
    bpos = idx & bvword_mask;
    bv->vec[wpos] |= (bvword_t)1 << bpos;
}

void
bitvec_clr_bit(bitvec_t *bv, size_t idx)
{
    size_t wpos;
    size_t bpos;

    if (idx >= bv->sz) {
        svc_die();
    }
    wpos = (idx + BIT_SIZE(bvword_t) - 1) / BIT_SIZE(bvword_t);
    bpos = idx & bvword_mask;
    bv->vec[wpos] &= ~((bvword_t)1 << bpos);
}

bool
bitvec_get_bit(bitvec_t *bv, size_t idx)
{
    size_t wpos;
    size_t bpos;

    if (idx >= bv->sz) {
        svc_die();
    }
    wpos = (idx + BIT_SIZE(bvword_t) - 1) / BIT_SIZE(bvword_t);
    bpos = idx & bvword_mask;
    return ((bv->vec[wpos] & (bvword_t)1 << bpos) != 0);
}
