#include "threadpool.h"
#include "mapreduce.h"
#include "stdlib.h"
#include "globals.h"
#include <stdlib.h>
#include <pthread.h>
#include <sys/stat.h>
#include <stdio.h>

/**
 * Function to create a new ThreadPool object
 * Parameters:
 *    num - Number of threads to create
 */
ThreadPool_t *ThreadPool_create(unsigned int num)
{
    // memory for the ThreadPool_t object
    ThreadPool_t *tp = (ThreadPool_t *)malloc(sizeof(ThreadPool_t));
    if (!tp)
        return NULL; // if mem alloc failed

    tp->destroy = 0;
    tp->threads = (pthread_t *)malloc(num * sizeof(pthread_t));

    // initialize the job queue
    tp->jobs.size = 0;
    tp->jobs.head = NULL;

    // allocate to the mutex locks
    tp->jobs.queue_mlock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    tp->thread_mlock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));

    // initializing the mutex locks
    pthread_mutex_init(tp->jobs.queue_mlock, NULL);
    pthread_mutex_init(tp->thread_mlock, NULL);

    // initializing the condition variables
    pthread_cond_init(&tp->check, NULL);
    pthread_cond_init(&tp->available_job_check, NULL);

    // creating the fixed amount of threads
    for (int i = 0; i < num; i++)
    {
        if (pthread_create(&tp->threads[i], NULL, (void *)Thread_run, tp) != 0)
        {
            perror("pthread_create");
            exit(1);
        }
    }

    // saving the total amount of threads created
    tp->total_threads = num;

    // idle jobs to amount of threads
    tp->idle_t = num;
    tp->finished_threads = 0;

    return tp;
}

/**
 * Function to destroy a ThreadPool object
 * Parameters:
 *     tp - Pointer to the ThreadPool object to be destroyed
 */
void ThreadPool_destroy(ThreadPool_t *tp)
{
    pthread_mutex_lock(tp->thread_mlock);
    tp->destroy = 1;
    // signal all threads to wake up to exit
    pthread_cond_broadcast(&tp->available_job_check);
    pthread_mutex_unlock(tp->thread_mlock);

    // destroy the threads by joining them back to the master
    for (int i = 0; i < tp->total_threads; i++)
    {
        pthread_join(tp->threads[i], NULL);
    }
    free(tp->threads); // must free memory allocated to threads

    // destroy the job queue
    ThreadPool_job_t *current = tp->jobs.head;
    while (current != NULL)
    {
        ThreadPool_job_t *next = current->next;
        free(current);
        current = next;
    }

    pthread_mutex_destroy(tp->jobs.queue_mlock);
    pthread_mutex_destroy(tp->thread_mlock);
    free(tp->jobs.queue_mlock);
    free(tp->thread_mlock);

    pthread_cond_destroy(&tp->check);
    pthread_cond_destroy(&tp->available_job_check);

    free(tp);
}

/**
 * Function to add a job to the ThreadPool's job queue
 * Parameters:
 *    tp   - Pointer to the ThreadPool object
 *    func - Pointer to the function that will be called by the serving thread
 *    arg  - Arguments for that function
 */
bool ThreadPool_add_job(ThreadPool_t *tp, thread_func_t func, void *arg)
{
    ThreadPool_job_t *new_job = (ThreadPool_job_t *)malloc(sizeof(ThreadPool_job_t));

    new_job->func = func; // ptr to the job its supposed to run
    new_job->arg = arg;   // args for func (filename) for mapper or partition
    new_job->next = NULL;

    // ThreadPool_job_t *current = tp->jobs.head;
    struct stat buff;
    int file_check = stat((char *)new_job->arg, &buff); // write file stats to buff

    // if its a mapper job
    if (file_check == 0)
    {
        new_job->length = buff.st_size; // getting the file size
    }
    // otherwise its a reducer job
    else
    {
        // save length to the size of the partition at arg (which is the index)
        new_job->length = ((part *)new_job->arg)->size;
    }

    pthread_mutex_lock(tp->jobs.queue_mlock);

    if (tp->jobs.size == 0)
    {
        tp->jobs.head = new_job;
    }
    else
    {
        ThreadPool_job_t *current = tp->jobs.head;
        ThreadPool_job_t *prev = NULL;

        // searching for where to put the job in the queue
        while (current != NULL && new_job->length >= current->length)
        {
            prev = current;
            current = current->next; // traverse the queue
        }

        // if prev is NULL means the job is the shortest
        if (prev == NULL)
        {
            new_job->next = tp->jobs.head;
            tp->jobs.head = new_job;
        }
        // otherwise we found the spot to insert the job
        else
        {
            new_job->next = current;
            prev->next = new_job;
        }
    }

    tp->jobs.size += 1;
    pthread_mutex_unlock(tp->jobs.queue_mlock);
    pthread_cond_signal(&tp->available_job_check);
    return true;
}

/**
 * Function to get a job from the job queue of the ThreadPool object
 * Parameters:
 *   tp - Pointer to the ThreadPool object
 * Return:
 *  ThreadPool_job_t* - Pointer to the job
 */
ThreadPool_job_t *ThreadPool_get_job(ThreadPool_t *tp)
{
    pthread_mutex_lock(tp->jobs.queue_mlock);

    ThreadPool_job_t *job = NULL;
    if(tp->jobs.size != 0)
    {
        job = tp->jobs.head; // assign the shortest job
        tp->jobs.size--;
        if (job)
            tp->jobs.head = job->next; // set head to next job in queue
    }

    pthread_mutex_unlock(tp->jobs.queue_mlock);
    return job;
}

/**
 * Thread function start routine
 * Parameters:
 *      tp - Pointer to the ThreadPool object
 */
void *Thread_run(ThreadPool_t *tp)
{
    while (true)
    {
        pthread_mutex_lock(tp->thread_mlock);
        // if destroy is set, exit the thread
        if (tp->destroy)
        {
            pthread_mutex_unlock(tp->thread_mlock);
            pthread_exit(NULL);
        }

        // wait for a signal that a job is available
        // on wake up, checks that job size increased, and that its not time to destory the tp
        while (tp->jobs.size == 0 && !tp->destroy)
        {
            pthread_cond_wait(&tp->available_job_check, tp->thread_mlock);
        }

        // if destroy is set, exit the thread
        if (tp->destroy)
        {
            pthread_mutex_unlock(tp->thread_mlock);
            pthread_exit(NULL);
        }

        // otherwise get the job
        ThreadPool_job_t *job = ThreadPool_get_job(tp);
        tp->idle_t -= 1;
        pthread_mutex_unlock(tp->thread_mlock);

        // if job is not null, run the job and free the job
        if (job != NULL)
        {
            job->func(job->arg);
            free(job);
        }

        pthread_mutex_lock(tp->thread_mlock);
        tp->idle_t += 1;
        pthread_cond_signal(&tp->check);
        pthread_mutex_unlock(tp->thread_mlock);
    }
}

/**
 * Function to check if all jobs are done
 * Parameters:
 *    tp - Pointer to the ThreadPool object
 */
void ThreadPool_check(ThreadPool_t *tp)
{
    pthread_mutex_lock(tp->thread_mlock);

    // while there is jobs or idle threads are less than total threads
    while (tp->jobs.size > 0 || tp->idle_t < tp->total_threads)
    {
        pthread_cond_wait(&tp->check, tp->thread_mlock);
    }
    pthread_mutex_unlock(tp->thread_mlock);
}