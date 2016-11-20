/*
 * Filename: pthread_util.h
 * Project: rpc-mt
 * Brief: Interface -- Miscellaneous pthread functions
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

#ifndef PTHREAD_UTIL_H
#define PTHREAD_UTIL_H 1

#ifdef  __cplusplus
extern "C" {
#endif

#include <pthread.h>

extern int pthread_mutex_is_locked(pthread_mutex_t *);
extern pthread_t pthread_get_owner(pthread_mutex_t *, pthread_t *);

#ifdef  __cplusplus
}
#endif

#endif /* PTHREAD_UTIL_H */
