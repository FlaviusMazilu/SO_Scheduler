#include "util/so_scheduler.h"
#include "my_header.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#define MAX_NR_THREADS 1000
#define PREEMPTED 69
tid_t create_new_tid();

sem_t micunealta;

enum Thread_state {
    BLOCKED,
    RUNNING,
    READY,
    WAITING,
    DONE
};
typedef struct so_task {
    enum Thread_state state;
    tid_t task_id;
    int priority;
    // pthread_cond_t cond;
    sem_t sem;
    so_handler *func;
    int cuanta;
} so_task_t;

typedef struct so_queue {
    so_task_t** array;
    int nr_elems;
    int max_priority;
} so_queue_t;


typedef struct scheduler {
    int time_quantum;
    int max_io_devices;
    so_queue_t so_queue;
    char number_of_instances;
} scheduler_t;

scheduler_t scheduler;

int get_thread_to_run(so_task_t *task)
{
    int rc;
    rc = pthread_create(&(task->task_id), NULL, task->func, NULL);
    return rc;
}

DECL_PREFIX int so_init(unsigned int time_quantum, unsigned int io)
{  
    scheduler.number_of_instances++;
    if (scheduler.number_of_instances > 1)
        return -1;
    if (io > SO_MAX_NUM_EVENTS)
        return -1;
    if (time_quantum == 0)
        return -1;

    scheduler.max_io_devices = io;
    scheduler.time_quantum = time_quantum;
    scheduler.so_queue.max_priority = -1;
    
    scheduler.so_queue.array = calloc(MAX_NR_THREADS, sizeof(so_task_t*));
        DIE(!scheduler.so_queue.array, "so_init malloc");
    
    sem_init(&micunealta, 0, 0);

    return 0;
}



void add_to_queue(so_task_t *task_to_add) {
    int i = 0;
    // checks where in the queue cand be the new thread added
    for (i = 0; i < scheduler.so_queue.nr_elems; i++) {
        if (task_to_add->priority > scheduler.so_queue.array[i]->priority)
            break;
    }

    for (int j = scheduler.so_queue.nr_elems; j > i; j--) {
        scheduler.so_queue.array[j] = scheduler.so_queue.array[j - 1]; 
    }

    // insert at the pos i
    scheduler.so_queue.array[i] = task_to_add;
    
    if (task_to_add->priority > scheduler.so_queue.max_priority)
        scheduler.so_queue.max_priority = task_to_add->priority;

    scheduler.so_queue.nr_elems++;

}
#define NOT_PREEMPTED 60
so_task_t* check_scheduler(pthread_t aux) {

    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        if (scheduler.so_queue.array[i]->state != BLOCKED) {
            // do some magic and run that thread
            // scheduler.so_queue.array[i]->func(scheduler.so_queue.array[i]->priority);
            if (aux == scheduler.so_queue.array[i]->task_id) {
                return NULL;
            }
            // sem_post(&(scheduler.so_queue.array[i]->sem));
            return scheduler.so_queue.array[i];
        }

    }
    return NULL;

}
void* start_thread(void *arg) {
    sem_post(&micunealta);

    so_task_t *task = (so_task_t*)arg;
    sem_wait(&(task->sem));

    task->func(task->priority);
    task->state = DONE;
    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        if (scheduler.so_queue.array[i]->state == READY) {
            sem_post(&(scheduler.so_queue.array[i]->sem));
            return NULL;
        }
    }
    return NULL;
}

DECL_PREFIX tid_t so_fork(so_handler *func, unsigned int priority)
{
    if (priority > SO_MAX_PRIO)
        return INVALID_TID;
    if (func == 0)
        return INVALID_TID;
    so_task_t *new_task = malloc(sizeof(*new_task));
    if (!new_task) {
        perror("so_fork");
        return -1;
    }
    new_task->func = func;
    new_task->priority = priority;
    new_task->state = READY;

    sem_init(&new_task->sem, 0, 0);

    add_to_queue(new_task);

    int rc = pthread_create(&(new_task->task_id), NULL, start_thread, new_task);
    sem_wait(&micunealta);

    DIE(rc != 0, "pthread create");
    so_task_t *current_task = NULL;
    tid_t my_tid = pthread_self();

    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        if (my_tid == scheduler.so_queue.array[i]->task_id) {
            current_task = scheduler.so_queue.array[i];
            break;
        }
    }
    so_task_t *res = check_scheduler(my_tid);
    if (res != NULL) {
        sem_post(&(res->sem));
        if (current_task) {
            sem_wait(&(current_task->sem));
        }
    }

   return new_task->task_id;
}



DECL_PREFIX void so_end(void)
{
    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        pthread_join(scheduler.so_queue.array[i]->task_id, NULL);
    }

    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        sem_destroy((&scheduler.so_queue.array[i]->sem));
        free(scheduler.so_queue.array[i]);
    }

    free(scheduler.so_queue.array);

    if (scheduler.number_of_instances > 0)
        scheduler.number_of_instances--;
    // free(&micunealta);
    sem_destroy(&micunealta);
    
}

DECL_PREFIX void so_exec(void)
{

}

DECL_PREFIX int so_signal(unsigned int io)
{
    return 0;
}

DECL_PREFIX int so_wait(unsigned int io)
{
    return 0;
}




