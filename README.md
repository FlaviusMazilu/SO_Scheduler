# **Threads Scheduler**

## About
- This project is meant to be a ``simulation`` of how a Round Robin with priorities Thread Scheduler works. Its threads are created with the ``POSIX`` API functions pthread_create and put to wait right after they are created.
- The ``synchronization`` between threads is implemented with ``semaphores`` for each thread

--- 

## Content
- `libscheduler.so` - a dynamic library that implements the scheduler functions
- `scheduler.c` - implementation for functionalities of the scheduler
- `so_scheduler.h` - signatures of the scheduler functions
- `Makefile` - builing the dynamic library

--- 
## Usage
- Build
```
make
```
- Export the library
```
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:.
```
