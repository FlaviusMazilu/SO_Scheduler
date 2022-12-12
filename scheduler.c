#include "so_scheduler.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#define MAX_NR_THREADS 1000
#define PREEMPTED 69
tid_t create_new_tid();

sem_t micunealta;
int cuanta_default;

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
    sem_t sem;
    int IO_id;
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
    so_queue_t all_threads;
} scheduler_t;

scheduler_t scheduler;

DECL_PREFIX int so_init(unsigned int time_quantum, unsigned int io)
{  
    scheduler.number_of_instances++;
    if (scheduler.number_of_instances > 1)
        return -1;
    if (io > SO_MAX_NUM_EVENTS)
        return -1;
    if (time_quantum == 0)
        return -1;

    // saves the initial time quantum in a global variable
    cuanta_default = time_quantum;

    scheduler.max_io_devices = io;
    scheduler.time_quantum = time_quantum;
    scheduler.so_queue.max_priority = -1;
    
    // so_queue.array= the threads in the priority queue
    scheduler.so_queue.array = calloc(MAX_NR_THREADS, sizeof(so_task_t*));
        DIE(!scheduler.so_queue.array, "so_init malloc");
    // saves all the threads even the ones finished
    scheduler.all_threads.array = calloc(MAX_NR_THREADS, sizeof(so_task_t*));
        DIE(!scheduler.all_threads.array, "so_init malloc");
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

// function used to put the preempted(for expiring time quantum) task in the right order
// in the queue
void requeue(so_task_t *task_to_remove, int pos)
{

    if (pos >= 0) {
        for (int j = pos; j < scheduler.so_queue.nr_elems - 1; j++) {
            scheduler.so_queue.array[j] = scheduler.so_queue.array[j + 1];
        }
        scheduler.so_queue.nr_elems--;
        add_to_queue(task_to_remove);
    }

}

// function used to check which thread should be running next
// is called after every instruction
// @return: NULL if the current thread should continue his execution, else the scheduled task
// is returned
so_task_t* check_scheduler(pthread_t aux) {

    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        if (scheduler.so_queue.array[i]->state == READY) {
            // do some magic and run that thread
            
            if (aux == scheduler.so_queue.array[i]->task_id) {
                return NULL;
            }
            return scheduler.so_queue.array[i];
        }

    }
    return NULL;

}

// wrapper for starting a thread, puts in on hold until its notified it can start executing
// the first instruction. A thread is done executing if his actual function handler returned 
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
    // makes the initializations for the structure of the new thread
    new_task->func = func;
    new_task->priority = priority;
    new_task->state = READY;
    new_task->IO_id = -1;
    new_task->cuanta = cuanta_default;

    sem_init(&new_task->sem, 0, 0);

    add_to_queue(new_task);
    scheduler.all_threads.array[scheduler.all_threads.nr_elems++] = new_task;
    int rc = pthread_create(&(new_task->task_id), NULL, start_thread, new_task);
    sem_wait(&micunealta);

    DIE(rc != 0, "pthread create");
    so_task_t *current_task = NULL;
    tid_t my_tid = pthread_self();

    // find the current task and it's position in the queue based on its tid
    int pos = -1;
    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        if (my_tid == scheduler.so_queue.array[i]->task_id) {
            current_task = scheduler.so_queue.array[i];
            pos = i;
            break;
        }
    }
    // in each instruction decrements the quantum
    if (current_task)
        current_task->cuanta--;
    
    if (current_task != NULL && current_task->cuanta <= 0) {
        current_task->cuanta = cuanta_default;
        requeue(current_task, pos);
    }

    so_task_t *res = check_scheduler(my_tid);
    // if the scheduled thread is not the current one, wake up the specific thread and
    // resets the current thread's quantum & waits
    if (res != NULL) {
        sem_post(&(res->sem));
        if (current_task) {
            current_task->cuanta = cuanta_default;
            sem_wait(&(current_task->sem));
        }
    }

   return new_task->task_id;
}

DECL_PREFIX void so_exec(void)
{
    tid_t my_tid = pthread_self();
    so_task_t *current_task;
    int pos = -1;
    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        if (my_tid == scheduler.so_queue.array[i]->task_id) {
            current_task = scheduler.so_queue.array[i];
            pos = i;
            break;
        }
    }

    if (current_task)
        current_task->cuanta--;
    
    if (current_task != NULL && current_task->cuanta <= 0) {
        current_task->cuanta = cuanta_default;
        requeue(current_task, pos);
    }

    so_task_t *res = check_scheduler(my_tid);
    if (res != NULL) {
        sem_post(&(res->sem));
        if (current_task) {
            current_task->cuanta = cuanta_default;
            sem_wait(&(current_task->sem));
        }
    }

}

DECL_PREFIX void so_end(void)
{
    // joins all the threads
    for (int i = 0; i < scheduler.all_threads.nr_elems; i++) {
        pthread_join(scheduler.all_threads.array[i]->task_id, NULL);
    }

    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        sem_destroy((&scheduler.so_queue.array[i]->sem));
        free(scheduler.so_queue.array[i]);
    }

    free(scheduler.so_queue.array);
    free(scheduler.all_threads.array);

    if (scheduler.number_of_instances > 0)
        scheduler.number_of_instances--;
    
    sem_destroy(&micunealta);
    
}


DECL_PREFIX int so_signal(unsigned int io)
{
    if (io >= scheduler.max_io_devices)
        return -1;
    
    tid_t myself = pthread_self();
    so_task_t *current_task;
    so_task_t *task_to_wake;

    // finds the position of the current task in the queue
    int pos = -1;
    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        if (scheduler.so_queue.array[i]->task_id == myself) {
            current_task = scheduler.so_queue.array[i];
            pos = i;
            break;
        }
    }
    // goes to the queue and tests if any of them are waiting for the io with the id io
    // if true, resets its IO_id and change their state from WAITING to READY
    int nr_threads_waken = 0;
    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        if (scheduler.so_queue.array[i]->IO_id == io) {
            task_to_wake = scheduler.so_queue.array[i];

            task_to_wake->IO_id = -1;
            task_to_wake->state = READY;
            nr_threads_waken++;
        }
    }

    if (current_task)
        current_task->cuanta--;
    
    if (current_task != NULL && current_task->cuanta <= 0) {
        current_task->cuanta = cuanta_default;
        requeue(current_task, pos);
    }

    so_task_t *res = check_scheduler(myself);
    if (res != NULL) {
        sem_post(&(res->sem));
        if (current_task) {
            current_task->cuanta = cuanta_default;
            sem_wait(&(current_task->sem));
        }
    }

    return nr_threads_waken;
}

DECL_PREFIX int so_wait(unsigned int io)
{
    if (io >= scheduler.max_io_devices)
        return -1;
    tid_t myself = pthread_self();
    so_task_t *current_task;
    int pos = -1;
    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        if (scheduler.so_queue.array[i]->task_id == myself) {
            current_task = scheduler.so_queue.array[i];
            pos = i;
            break;
        }
    }

    // puts the thread in the waiting state and sets the id io he is waiting for
    if (current_task) {
        current_task->IO_id = io;
        current_task->state = WAITING;
    }

    
    if (current_task)
        current_task->cuanta--;
    
    if (current_task != NULL && current_task->cuanta <= 0) {
        current_task->cuanta = cuanta_default;
        requeue(current_task, pos);
    }

    so_task_t *res = check_scheduler(myself);
    if (res != NULL) {
        sem_post(&(res->sem));
        if (current_task) {
            current_task->cuanta = cuanta_default;
            sem_wait(&(current_task->sem));
        }
    }

    return 0;
}




