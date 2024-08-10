#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include "config.h"

#define MAX_QUEUE 100  // Maximum task queue length

typedef struct {
    int client_fd;
    struct Config *config;
} Task;  // Define the task structure, containing client file descriptor and config pointer

typedef struct {
    Task queue[MAX_QUEUE];  // Task queue
    int front;  // Queue head index
    int rear;  // Queue tail index
    int count;  // Number of tasks in the queue
    int connected_clients;  // Newly added member variable, used to record the number of connected clients
    pthread_mutex_t mutex;  // Mutex lock to protect the task queue
    pthread_cond_t cond;  // Condition variable to notify threads of new tasks
    pthread_t *threads;  // Thread array
    int num_threads;  // Number of threads
    int shutdown;  // Thread pool shutdown flag
    int client_fds[MAX_QUEUE]; // Client connection list
    int client_count; // Number of clients
} ThreadPool;

void init_threadpool(ThreadPool *pool, int num_threads);  // Initialize the thread pool
void add_task(ThreadPool *pool, Task task);  // Add a task to the task queue
void *worker_thread(void *arg);  // Thread worker function
void destroy_threadpool(ThreadPool *pool);  // Destroy the thread pool
void close_all_clients(ThreadPool *pool); // Close all clients

#endif // THREADPOOL_H
