/*
 * Part of the solution for Final project Challenge 1, by Guan Wang, Dan Luo and Hind Djebien.

 * Initiate and manage the thread pool, maximum thread depend on parameter THMAX
 * Could add new thread and close thread pool properly if it needs
 */


#include <stdio.h>
#include <stdlib.h>
#include "threadpool.h"
#include "fserv.h"


void error_handling(const char* message);

void init_threadpool(ThreadPool *pool, int num_threads) {
    pool->front = 0;
    pool->rear = 0;
    pool->count = 0;
    pool->connected_clients = 0;
    pool->num_threads = num_threads;//pool.num_thread is bbfile.T defined by user
    pool->shutdown = 0;
    pthread_mutex_init(&pool->mutex, NULL);  // Initialize the mutex
    pthread_cond_init(&pool->cond, NULL);  // Initialize condition variables
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * num_threads);  // Allocate thread array memory

    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {  // create thread for client server
            error_handling("pthread_create"); // Thread creation failed, exit the program
        }
    }
}

void add_task(ThreadPool *pool, Task task) {
    pthread_mutex_lock(&pool->mutex);  // lock

    if (pool->count < MAX_QUEUE) {  // Check if the task queue is full
        pool->queue[pool->rear] = task;  // Add tasks to the end of the queue
        pool->client_fds[pool->client_count++] = task.client_fd; // Add client connections to the list
        pool->rear = (pool->rear + 1) % MAX_QUEUE;  // Update the queue tail index
        pool->count++;  // Update task count
        pthread_cond_signal(&pool->cond);  // Notify waiting threads of new tasks
    }

    pthread_mutex_unlock(&pool->mutex);  // unlock
}

void *worker_thread(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;

    while (1) {
        pthread_mutex_lock(&pool->mutex);  // lock

        while (pool->count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->mutex);  // Waiting for the task to arrive or the thread pool to close
        }

        if (pool->shutdown && pool->count == 0) {  // Check if the thread pool is closed
            pthread_mutex_unlock(&pool->mutex);  // unlock
            pthread_exit(NULL);  // end the thread

        }

        //After received the conditional variable, start the client server task
        Task task = pool->queue[pool->front];  // Get the task at the head of the task queue
        pool->front = (pool->front + 1) % MAX_QUEUE;  // Update the queue head index
        pool->count--;  // Update task count
        pool->connected_clients++; // Update connected client count

        pthread_mutex_unlock(&pool->mutex);  // unlock

        struct ClientArgs client_args = {task.client_fd, task.config, pool};
        handle_client((void *)&client_args);  // handle the client server task

        pthread_mutex_lock(&pool->mutex);
        pool->connected_clients--;  // after client unconnected, update the connected client count
        pthread_mutex_unlock(&pool->mutex);
    }

    return NULL;
}

void destroy_threadpool(ThreadPool *pool) {
    pool->shutdown = 1;  // Set the close flag
    pthread_cond_broadcast(&pool->cond);  // Notify all threads

    for (int i = 0; i < pool->num_threads; i++) {
        int p_join = pthread_join(pool->threads[i], NULL);  // Wait for all threads to exit
        if(p_join !=0){
            perror("pthread join");
        }

    }

    close_all_clients(pool); // Close all client connections

    free(pool->threads);  // Release thread array memory
    pthread_mutex_destroy(&pool->mutex);  // Destroy mutex
    pthread_cond_destroy(&pool->cond);  // Destroy condition variable
}

void close_all_clients(ThreadPool *pool) { // close all the client
//    pthread_mutex_lock(&pool->mutex); // lock
    for (int i = 0; i < pool->client_count; i++) {
//        printf("%d\n",i); //debug, ensure thread of closing client
        close(pool->client_fds[i]); // close all client socket
    }
//    pthread_mutex_unlock(&pool->mutex); // unlock
}