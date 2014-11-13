
#include "util.h"

/*
 * Generally handy functions that, perhaps, should have been
 * in libc.
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

