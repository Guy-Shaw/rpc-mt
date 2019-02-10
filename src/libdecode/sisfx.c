/*
 * Filename: sisfx.c
 * Project: libdecode
 * Library: libdecode
 * Brief: Print a number as a small magnitude followed by an SI suffix scale
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

#include <decode-impl.h>

#include <stddef.h>
    // Import constant NULL
#include <stdint.h>
    // Import type uint32_t
    // Import type uint64_t
    // Import type uintptr_t
#include <stdio.h>
    // Import sprintf()

/*
 * Decode a number as a small number with an SI suffix.
 *
 * OK, so it is really binary, because we deal in multiples of 1024
 * instead of 1000, but there is no need here to get involved in those
 * fancy NIST binary units (Kibi, mebi, gibi, tebi, pebi, exbi)
 * and suffixes (Ki, Mi, Gi, Ti, Pi, Ei).  No, we don't do that here.
 */

/*
 * Format a number that has already been range-reduced
 * and broken down into a numer, |n|, in 0..1023 and a magnitude, |mag|.
 * The original raw number would be n * 1024**mag.
 *
 * The result buffer needs to be big enough to hold a range-reduced
 * decimal number and a one character SI suffix.  8 bytes is plenty.
 *
 * SI suffixes are:
 *     K = kilo    1024**1  ~ 10**3
 *     M = mega    1024**2  ~ 10**6
 *     G = giga    1024**3  ~ 10**9
 *     T = tera    1024**4  ~ 10**12
 *     P = peta    1024**5  ~ 10**15
 *     E = exa     1024**6  ~ 10**18
 */
char *
sisfx_scaled_r(char *result, uint_t n, uint_t mag)
{
    static char sfx[] = { 0, 'K', 'M', 'G', 'T', 'P', 'E' };
    uint_t frac;

    frac = 0;
    while (n > 1023) {
        ++mag;                  /* Move up the SI suffix ladder */
        frac = n & 1023;        /* Save 10 bits of fraction */
        n >>= 10;               /* Range reduce by 10 bits */
    }
    if (mag == 0) {
        sprintf(result, "%u", n);
        return (result);
    }

    /*
     * If there is only one significant digit of magnitude, without
     * considering any fraction, and the fractional part is not '.0'
     * (considering only the first decimal digit of the fraction),
     * then include the first digit of the fraction, in order not to
     * lose so much precision.  We are interested in a "good" trade-off
     * between length of representation and precision, and so, for our
     * purposes, we are not interested in any more than 1 digit of
     * fraction.
     */
    if (n < 10 && frac > 99)
        sprintf(result, "%u.%u%c", n, frac / 100, sfx[mag]);
    else
        sprintf(result, "%u%c", n, sfx[mag]);
    return (result);
}

/*
 * Decode a 32-bit number as a small number with an SI suffix.
 */

char *
sisfx32_r(char *res, uint_t n)
{
    return (sisfx_scaled_r(res, n, 0));
}

/*
 * Decode a 32-bit number as a small number with an SI suffix.
 * Just do range reduction to a mantissa in 0..2**32, and then
 * let the 32-bit logic do the rest.
 */

char *
sisfx64_r(char *result, uint64_t n64)
{
    uint_t n32;
    uint32_t mag;

    mag = 0;
    while (n64 > ((uint64_t)1 << 30ULL)) {
        n64 >>= 10ULL;          /* Reduce the mantissa */
        ++mag;                  /* Move up the SI suffix ladder */
    }
    n32 = n64;
    return (sisfx_scaled_r(result, n32, mag));
}
