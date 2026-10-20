//
// ------------------------------------
// Jennifer Taft
// rwlock.c
// ------------------------------------
//
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include "rwlock.h"

typedef struct rwlock{
	pthread_mutex_t mutex;
    pthread_cond_t cond_readers;
    pthread_cond_t cond_writers;
    
    //readers in critical section
    int active_readers;  
    //0 or 1 to determine active writer
    int active_writers;  
    
    //readers waiting for lock
    int waiting_readers;
    //writers waiting for lock  
    int waiting_writers; 

    PRIORITY priority;
    int n;          
    
    //tracks consecutive acquisitions
    int consecutive;      
    // 0 = readers, 1 = writers, -1 no previous type
    int last_type;
} rwlock;

/** @brief Dynamically allocates and initializes a new rwlock with
 *         priority p, and, if using N_WAY priority, n.
 *
 *  @param The priority of the rwlock
 *
 *  @param The n value, if using N_WAY priority
 *
 *  @return a pointer to a new rwlock_t
 */

rwlock_t *rwlock_new(PRIORITY p, uint32_t n){
	rwlock_t *rw = malloc(sizeof(rwlock_t));
    if (!rw)
        return NULL;
    
    pthread_mutex_init(&rw->mutex, NULL);
    pthread_cond_init(&rw->cond_readers, NULL);
    pthread_cond_init(&rw->cond_writers, NULL);

    rw->active_readers = 0;
    rw->active_writers = 0;
    rw->waiting_readers = 0;
    rw->waiting_writers = 0;
    rw->priority = p;
    rw->n = n;
    rw->consecutive = 0;
    rw->last_type = -1;

    return rw;
}

/** @brief Delete your rwlock and free all of its memory.
 *
 *  @param rw the rwlock to be deleted.  Note, you should assign the
 *  passed in pointer to NULL when returning (i.e., you should set *rw
 *  = NULL after deallocation).
 *
 */
void rwlock_delete(rwlock_t **rw){
	if (rw == NULL || *rw == NULL)
        return;
    
    rwlock_t *l = *rw;
    pthread_mutex_destroy(&l->mutex);
    pthread_cond_destroy(&l->cond_readers);
    pthread_cond_destroy(&l->cond_writers);
    free(l);
    *rw = NULL;
}

/** @brief acquire rw for reading
 *
 */
void reader_lock(rwlock_t *rw){
	if (rw == NULL){
		return;
	}
    
    pthread_mutex_lock(&rw->mutex);
    rw->waiting_readers++;
    
    while (rw->active_writers > 0 ||
           //wait if any writer is waiting 
           (rw->priority == WRITERS && rw->waiting_writers > 0) ||
           //if readers have run consecutively for n times and writers are waiting
           (rw->priority == N_WAY && rw->last_type == 0 && rw->consecutive >= rw->n && rw->waiting_writers > 0)) {
        pthread_cond_wait(&rw->cond_readers, &rw->mutex);
    }
    
    rw->waiting_readers--;
    
    if (rw->priority == N_WAY) {
        if (rw->last_type == 0)
            rw->consecutive++;
        else {
            rw->last_type = 0;
            rw->consecutive = 1;
        }
    }
    rw->active_readers++;
    pthread_mutex_unlock(&rw->mutex);
}

/** @brief release rw for reading--you can assume that the thread
 * releasing the lock has *already* acquired it for reading.
 *
 */
void reader_unlock(rwlock_t *rw){
	if (rw == NULL)
        return;
    
    pthread_mutex_lock(&rw->mutex);
    rw->active_readers--;
    
    if (rw->active_readers == 0) {
        if (rw->priority == N_WAY && rw->last_type == 0 &&
            rw->consecutive >= rw->n && rw->waiting_writers > 0) {
            rw->consecutive = 0;
            rw->last_type = -1;
            pthread_cond_signal(&rw->cond_writers);
        } else {
           	//Writers
            if (rw->priority == WRITERS && rw->waiting_writers > 0)
                pthread_cond_signal(&rw->cond_writers);
            else {
                pthread_cond_broadcast(&rw->cond_readers);
                pthread_cond_signal(&rw->cond_writers);
            }
        }
    }
    
//     //if no more active readers, signal writer
//     if (rw->active_readers == 0)
//         pthread_cond_signal(&rw->cond_writers);
    
    pthread_mutex_unlock(&rw->mutex);
}

/** @brief acquire rw for writing
 *
 */
void writer_lock(rwlock_t *rw){
    if (rw == NULL)
        return;
    
    pthread_mutex_lock(&rw->mutex);
    rw->waiting_writers++;
    
    while (rw->active_writers > 0 || rw->active_readers > 0 ||
           /* READERS priority: yield to waiting readers */
           (rw->priority == READERS && rw->waiting_readers > 0) ||
           /* N_WAY: if writers have run consecutively for n times and readers are waiting */
           (rw->priority == N_WAY && rw->last_type == 1 && rw->consecutive >= rw->n && rw->waiting_readers > 0)) {
        pthread_cond_wait(&rw->cond_writers, &rw->mutex);
    }
    
    rw->waiting_writers--;
    
    if (rw->priority == N_WAY) {
        if (rw->last_type == 1)
            rw->consecutive++;
        else {
            rw->last_type = 1;
            rw->consecutive = 1;
        }
    }
    rw->active_writers = 1;
    pthread_mutex_unlock(&rw->mutex);
}

/** @brief release rw for writing--you can assume that the thread
 * releasing the lock has *already* acquired it for writing.
 *
 */
void writer_unlock(rwlock_t *rw){
	if (rw == NULL)
        return;
    
    pthread_mutex_lock(&rw->mutex);
    rw->active_writers = 0;
    
    if (rw->priority == N_WAY && rw->last_type == 1 &&
        rw->consecutive >= rw->n && rw->waiting_readers > 0) {
        rw->consecutive = 0;
        rw->last_type = -1;
        pthread_cond_broadcast(&rw->cond_readers);
    } else {
        if (rw->priority == WRITERS && rw->waiting_writers > 0)
            pthread_cond_signal(&rw->cond_writers);
        else {
            pthread_cond_broadcast(&rw->cond_readers);
            pthread_cond_signal(&rw->cond_writers);
        }
    }
    
//     if (rw->priority == WRITERS) {
//         /* Give preference to writers */
//         if (rw->waiting_writers > 0)
//             pthread_cond_signal(&rw->cond_writers);
//         else
//             pthread_cond_broadcast(&rw->cond_readers);
//     } else if (rw->priority == READERS) {
//         /* Readers are favored */
//         if (rw->waiting_readers > 0)
//             pthread_cond_broadcast(&rw->cond_readers);
//         else
//             pthread_cond_signal(&rw->cond_writers);
//     } else {  /* N_WAY mode */
//         if (rw->last_type == 1 && rw->waiting_readers > 0 && rw->consecutive >= rw->n) {
//             /* Reset the consecutive count to allow readers */
//             rw->consecutive = 0;
//             rw->last_type = -1;
//             pthread_cond_broadcast(&rw->cond_readers);
//         } else if (rw->last_type == 0 && rw->waiting_writers > 0 && rw->consecutive >= rw->n) {
//             /* Reset to allow writers */
//             rw->consecutive = 0;
//             rw->last_type = -1;
//             pthread_cond_signal(&rw->cond_writers);
//         } else {
//             /* If no limit is reached, wake both sides */
//             pthread_cond_broadcast(&rw->cond_readers);
//             pthread_cond_signal(&rw->cond_writers);
//         }
//     }
    pthread_mutex_unlock(&rw->mutex);
}
