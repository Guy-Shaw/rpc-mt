/*
 * Filename: pthread_util.c
 * Project: rpc-mt
 * Library: librpc
 * Brief: Generally handy pthread functions
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

#include "pthread_util.h"

/*
 * Generally handy pthread-related functions that, perhaps,
 * should have been in libc.
 */

int
pthread_mutex_is_locked(pthread_mutex_t *lock)
{
    int ret;

    ret = pthread_mutex_trylock(lock);
    if (ret == 0) {
        pthread_mutex_unlock(lock);
        return (0);
    }
    return (1);
}

/*
 * Return the owner thread of the given { lock, owner } pair.
 *
 * The value of @var{owner} is valid, only if @var{lock} is currently locked.
 */
pthread_t
pthread_get_owner(pthread_mutex_t *lock, pthread_t *owner)
{
    int ret;

    ret = pthread_mutex_trylock(lock);
    if (ret == 0) {
        pthread_mutex_unlock(lock);
        return ((pthread_t)NULL);
    }
    return (*owner);
}
