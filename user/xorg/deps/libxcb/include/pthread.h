#ifndef OPEN_SILLICON_XCB_PTHREAD_H
#define OPEN_SILLICON_XCB_PTHREAD_H

/* XCB's single-thread build on OpenSillicon.  This is intentionally not a
 * general pthread implementation: the OS currently has no user threads. */
typedef struct { unsigned char unused; } pthread_mutex_t;
typedef struct { unsigned char unused; } pthread_cond_t;
typedef unsigned long pthread_t;
typedef unsigned long pthread_key_t;
#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_COND_INITIALIZER { 0 }
static inline int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr)
{ (void)mutex; (void)attr; return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *mutex)
{ (void)mutex; return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *mutex)
{ (void)mutex; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *mutex)
{ (void)mutex; return 0; }
static inline int pthread_cond_init(pthread_cond_t *condition, const void *attr)
{ (void)condition; (void)attr; return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *condition)
{ (void)condition; return 0; }
static inline int pthread_cond_signal(pthread_cond_t *condition)
{ (void)condition; return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *condition)
{ (void)condition; return 0; }
static inline int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex)
{ (void)condition; (void)mutex; return 0; }

#endif
