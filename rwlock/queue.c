//
// ------------------------------------
// Jennifer Taft
// queue.c
// ------------------------------------
//
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include "queue.h"

/** @struct queue_t
 *
 *  @brief This typedef renames the struct queue.  Your `c` file
 *  should define the variables that you need for your queue.
 */
typedef struct queue{
	//total number of elements for queue
	int size;
	//current number of elements
    int count; 
    //index of next element to pop          
    int head;
    //index of next slot to push           
    int tail; 
    //array to store elements (void pointers rn)          
    void **buffer;       

	//protects access to queue
    pthread_mutex_t mutex;  
    //signaled when queue is not empty
    pthread_cond_t not_empty;     
    //signaled when queue is not full
    pthread_cond_t not_full; 
} queue;

/** @brief Dynamically allocates and initializes a new queue with a
 *         maximum size, size
 *
 *  @param size the maximum size of the queue
 *
 *  @return a pointer to a new queue_t
 */
queue_t *queue_new(int size){
	if (size <= 0)
        return NULL;

    queue_t *q = malloc(sizeof(queue_t));
    if (!q)
        return NULL;

    q->size = size;
    q->count = 0;
    q->head = 0;
    q->tail = 0;

    q->buffer = malloc(sizeof(void *) * size);
    if (!q->buffer) {
        free(q);
        return NULL;
    }

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);

    return q;
}

/** @brief Delete your queue and free all of its memory.
 *
 *  @param q the queue to be deleted.  Note, you should assign the
 *  passed in pointer to NULL when returning (i.e., you should set
 *  *q = NULL after deallocation).
 *
 */
void queue_delete(queue_t **q){
	if (q == NULL || *q == NULL)
        return;

    queue_t *q_ptr = *q;

    pthread_mutex_destroy(&q_ptr->mutex);
    pthread_cond_destroy(&q_ptr->not_empty);
    pthread_cond_destroy(&q_ptr->not_full);

    free(q_ptr->buffer);
    free(q_ptr);

    *q = NULL;
}

/** @brief push an element onto a queue
 *
 *  @param q the queue to push an element into.
 *
 *  @param elem th element to add to the queue
 *
 *  @return A bool indicating success or failure.  Note, the function
 *          should succeed unless the q parameter is NULL.
 */
bool queue_push(queue_t *q, void *elem){
    if (q == NULL)
        return false;

    pthread_mutex_lock(&q->mutex);

	//wait until there is space
    while (q->count == q->size)
        pthread_cond_wait(&q->not_full, &q->mutex);

    //add to tail, update q->tail
    q->buffer[q->tail] = elem;
    q->tail = (q->tail + 1) % q->size;
    q->count++;

    //signal queue is not empty
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);

    return true;
}

/** @brief pop an element from a queue.
 *
 *  @param q the queue to pop an element from.
 *
 *  @param elem a place to assign the poped element.
 *
 *  @return A bool indicating success or failure.  Note, the function
 *          should succeed unless the q parameter is NULL.
 */
bool queue_pop(queue_t *q, void **elem){
    if (q == NULL)
        return false;

    pthread_mutex_lock(&q->mutex);

    //wait until at least one element
    while (q->count == 0)
        pthread_cond_wait(&q->not_empty, &q->mutex);

    //retrieve head, update q->head
    *elem = q->buffer[q->head];
    q->head = (q->head + 1) % q->size;
    q->count--;

    //signal queue has space
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);

    return true;
}
