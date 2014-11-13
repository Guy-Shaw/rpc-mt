/*
 * File: util.h
 * 
 */

#ifndef _UTIL_H
#define _UTIL_H 1

#include <pthread.h>

extern int pthread_mutex_is_locked(pthread_mutex_t *);
extern pthread_t pthread_get_owner(pthread_mutex_t *, pthread_t *);

#endif /* _UTIL_H */
