/*
 * Filename: int_limits.h
 * Project: rpc-mt
 * Brief: helper macros to determine min and max values for integral types.
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

#ifndef _INT_LIMITS_H
#define _INT_LIMITS_H 1

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * These macros generate compile-time constants for minimum and maximum
 * values for integral types.
 *
 * __MIN() and __MAX() work for both signed and unsign integers of
 * all sizes of the standard integral types.  One exception is if there
 * are integral types that are longer than intmax_t or uintmax_t.
 * For example, GNU gcc might support @type{__int128_t}, or something
 * like that.  These macros probably will not work on those.
 *
 * The value of these macros is that they can work portably on
 * abstract types, even when the underlying type is defined by
 * a library and even if size or signedness of the underlying
 * primitive type changes.
 *
 */

#define issigned(t) ((t) -1 < 0)

#define __HALF_MAX_SIGNED(type) ((type)1 << (sizeof(type) * 8 - 2))
#define __MAX_SIGNED(type) (__HALF_MAX_SIGNED(type) - 1 + __HALF_MAX_SIGNED(type))
#define __MIN_SIGNED(type) (-1 - __MAX_SIGNED(type))

#define __MIN(type) ((type)-1 < 1?__MIN_SIGNED(type):(type)0)
#define __MAX(type) ((type)~__MIN(type))


#ifdef  __cplusplus
}
#endif

#endif /* _INT_LIMITS_H */
