/*
 * Filename: bitvec.h
 * Project: rpc-mt
 * Brief: bit vector implementation - just enough to replace fd_set functions
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

#ifndef _BITVEC_H
#define _BITVEC_H 1

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdbool.h>
    // Import type bool
#include <unistd.h>
    // Import type size_t

typedef size_t bvword_t;

struct bitvec {
    bvword_t *vec;  // Actual space allocated for the bit vector
    size_t    sz;   // Size in bits
};

typedef struct bitvec bitvec_t;

/*
 * Initializes the |bitvec_t| to handle the given number of elements, |nbits|.
 * Should be called before using the |bitvec_t|.
 *
 * @param bv    the |bitvec_t| to initialize
 * @param nbits the number of bits the |bitvec_t| is to be able to handle
 */
extern void bitvec_init(bitvec_t *bv, size_t nbits);

/*
 * Destructor for |bitvec_t|.
 * Frees all resources owned by the given |bitvec_t|.
 * Should be called after work with the |bitvec_t| is done.
 *
 * @param bv the |bitvec_t| to destroy
 */
extern void bitvec_free(bitvec_t *bv);

/*
 * Set |idx|-th bit of |bv|.
 * Set the bit at bit-index |idx| in |bitvec_t| |bv| to 1.
 *
 * @param bv   the bitvec_t which is to have a bit set
 * @param idx  the particular bit which is to be set
 */
extern void bitvec_set_bit(bitvec_t *bv, size_t idx);

/*
 * Clear the |idx|-th bit of |bv|.
 * Set the bit at bit-index |idx| in |bitvec_t| |bv| to 0.
 *
 * @param bv   the bitvec_t which is to have a bit set
 * @param idx  the particular bit which is to be set
 */

extern void bitvec_clr_bit(bitvec_t *bv, size_t idx);

/*
 * Get the value of the bit at bit-index |idx| of |bitvec_t| |bv|.
 *
 * @param bv   the bitvec_t from which to get the value
 * @param idx  the particular bit which is to be gotten
 * @return     the value of bit at bit position, |idx| in |bv|.
*/
extern bool bitvec_get_bit(bitvec_t *bv, size_t idx);

#ifdef  __cplusplus
}
#endif

#endif /* _BITVEC_H */
