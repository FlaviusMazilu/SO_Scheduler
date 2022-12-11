
#include "checker-lin/_test/scheduler_test.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include "my_header.h"
FILE *f;
#define MAX_NR_THREADS 1000
#define PREEMPTED 69
tid_t create_new_tid();
sem_t semafor_universal;

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
    
    sem_init(&semafor_universal, 0, 0);
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
    sleep(1);
    sem_post(&semafor_universal);
    so_task_t *task = (so_task_t*)arg;
    
    printf("[TID %d]: goes to sleep(waits)\n", pthread_self() % 100);
    sem_wait(&(task->sem));
    sleep(1);
    printf("[TID %d]: receives post\n", pthread_self() % 100);

    task->func(task->priority);
    task->state = DONE;
    printf("---> over %d\n", pthread_self() % 100);

    for (int i = 0; i < scheduler.so_queue.nr_elems; i++) {
        if (scheduler.so_queue.array[i]->state == READY) {
            sem_post(&(scheduler.so_queue.array[i]->sem));
            return NULL;
        }
    }
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
    printf("thread %d created thread %d\n", pthread_self() % 100, new_task->task_id % 100);
    sem_wait(&semafor_universal);
    // sleep(1);
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
    if (res)
        printf("[TID %d]: check_scheduler: %d TID was scheduled\n", pthread_self() % 100, res->task_id % 100);
    else
        printf("[TID: %d], still me %d\n", pthread_self()% 100, pthread_self()% 100);
    if (res != NULL) {
        printf("[TID %d]: wakes up thread: %d\n", pthread_self() % 100, res->task_id % 100);
        sem_post(&(res->sem));
        if (current_task) {
            printf("[TID %d]: goes to sleep\n", pthread_self() % 100);
            sem_wait(&(current_task->sem));
            sleep(1);
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




static unsigned int test_sched_fork_handler_runs;
static unsigned int test_sched_fork_runs;

static void test_sched_handler_10_worker(unsigned int prio)
{
	test_sched_fork_handler_runs++;
}

static void test_sched_handler_10_master(unsigned int prio)
{
	unsigned int run;

	for (run = 0; run < test_sched_fork_runs; run++)
		so_fork(test_sched_handler_10_worker, 0);
}

void test_sched_10(void)
{
	// test_sched_fork_runs = get_rand(1, SO_MAX_UNITS);
    test_sched_fork_runs = 2;
	if (so_init(SO_MAX_UNITS, 0) < 0) {
		so_error("initialization failed");
		goto test;
	}

	so_fork(test_sched_handler_10_master, SO_MAX_PRIO);
	sched_yield();

test:
	so_end();

	// basic_test(test_sched_fork_handler_runs == test_sched_fork_runs);
}

/*
 * 11) Test multiple fork thread ids
 *
 * tests if the scheduler runs each fork with a different id
 */
static unsigned int test_fork_rand_tests;
static unsigned int test_fork_execution_status;
static tid_t test_fork_exec_tids[SO_MAX_UNITS];
static tid_t test_fork_tids[SO_MAX_UNITS];

static void test_sched_handler_11_worker(unsigned int dummy)
{
	static unsigned int exec_idx;

	/* signal that he's the one that executes in round exec_idx */
	test_fork_exec_tids[exec_idx++] = get_tid();
	test_fork_execution_status = SO_TEST_SUCCESS;
}

static void test_sched_handler_11_master(unsigned int dummy)
{
	unsigned int i;

	/*
	 * this thread should not be preempted as it executes maximum
	 * SO_MAX_UNITS - 1, and the quantum time is SO_MAX_UNITS
	 */
	/* use a cannary value to detect overflow */
	test_fork_tids[test_fork_rand_tests] = get_tid();
	test_fork_exec_tids[test_fork_rand_tests] = get_tid();
	for (i = 0; i < test_fork_rand_tests; i++)
		test_fork_tids[i] = so_fork(test_sched_handler_11_worker, 0);
}

void test_sched_11(void)
{
	unsigned int i;

	test_fork_rand_tests = get_rand(1, SO_MAX_UNITS - 1);
	test_fork_execution_status = SO_TEST_FAIL;

	so_init(SO_MAX_UNITS, 0);

	so_fork(test_sched_handler_11_master, 0);

	sched_yield();
	so_end();

	if (test_fork_execution_status == SO_TEST_SUCCESS) {
		/* check threads order */
		for (i = 0; i <= test_fork_rand_tests; i++) {
			if (!equal_tids(test_fork_exec_tids[i],
				test_fork_tids[i])) {
				so_error("different thread ids");
				test_fork_execution_status = SO_TEST_FAIL;
				break;
			}
		}
	} else {
		so_error("threads execution failed");
	}

	// basic_test(test_fork_execution_status);
}

int main()
{

    test_sched_10();
    return 0;
}