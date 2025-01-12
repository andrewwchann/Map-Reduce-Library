#include "mapreduce.h"
#include "threadpool.h"
#include "globals.h"
#include <stdlib.h>
#include <string.h>

extern void Reduce(char *key, int partition_number);

// create the global partition shell
part **partitions;
unsigned int num_partitions;

/**
 * Run the MapReduce framework
 *
 * Parameters:
 *   file_count   - Number of files (i.e. input splits)
 *   file_names   - Array of filenames
 *   mapper       - Function pointer to the map function
 *   reducer      - Function pointer to the reduce function
 *   num_workers  - Number of threads in the thread pool
 *   num_parts    - Number of partitions to be created
 */
void MR_Run(unsigned int file_count, char *file_names[],
            Mapper mapper, Reducer reducer,
            unsigned int num_workers, unsigned int num_parts)
{
    // create thread pool
    ThreadPool_t *tp = ThreadPool_create(num_workers);

    num_partitions = num_parts;                               // set global num_partitions
    partitions = (part **)malloc(num_parts * sizeof(part *)); // allocate memory for partitions
    for (int i = 0; i < num_parts; i++)
    {
        partitions[i] = (part *)malloc(sizeof(part)); // allocate to each partition
        partitions[i]->size = 0;
        partitions[i]->head = NULL;
        partitions[i]->part_index = i;
        pthread_mutex_init(&partitions[i]->part_mlock, NULL); // init mutex lock for each partition
    }

    // add mapper jobs to thread pool
    for (int i = 0; i < file_count; i++)
        ThreadPool_add_job(tp, (thread_func_t)mapper, (void *)file_names[i]);

    // check if all done using threadpool_check
    ThreadPool_check(tp);

    // by here, partitions should be full of key, value pairs
    // add reducer jobs
    for (int i = 0; i < num_parts; i++)
    {
        ThreadPool_add_job(tp, (thread_func_t)MR_Reduce, (void *)partitions[i]);
    }

    // check if all done then kill it all
    ThreadPool_check(tp);
    if (tp->idle_t == tp->total_threads)
    {
        ThreadPool_destroy(tp);
    }

    // destroying the partitions
    for (unsigned int i = 0; i < num_partitions; i++)
    {
        pthread_mutex_lock(&partitions[i]->part_mlock);
        pair *current = partitions[i]->head;

        while (current != NULL)
        {
            pair *next_head = current->next_head;

            // Free all pairs in the current key group
            pair *pair_current = current;
            while (pair_current != NULL)
            {
                pair *next_pair = pair_current->next_pair;
                free(pair_current->key);
                free(pair_current->value);
                free(pair_current);
                pair_current = next_pair;
            }

            current = next_head;
        }

        pthread_mutex_unlock(&partitions[i]->part_mlock);
        pthread_mutex_destroy(&partitions[i]->part_mlock);
        free(partitions[i]); // Free the partition struct
    }
    free(partitions);
}

/**
 * Write a specifc map output, a <key, value> pair, to a partition
 * Parameters:
 key
 value- Key of the output- Value of the output
 */
void MR_Emit(char *key, char *value)
{
    // partition the key and get the partition index
    unsigned long partition_i = MR_Partitioner(key, num_partitions);

    // lock the partition
    pthread_mutex_lock(&partitions[partition_i]->part_mlock);

    // add the key-value pair to the partition
    pair *new_pair = (pair *)malloc(sizeof(pair));
    new_pair->key = strdup(key);
    new_pair->value = strdup(value);
    new_pair->next_pair = NULL;
    new_pair->next_head = NULL;

    // if the partition is empty, add the pair to the head
    if (partitions[partition_i]->size == 0)
    {
        partitions[partition_i]->head = new_pair;
    }
    else
    {
        pair *current = partitions[partition_i]->head;
        pair *prev = NULL;
        while (current != NULL)
        {
            // if we found the key group, add the key into it
            if (strcmp(current->key, new_pair->key) == 0)
            {
                new_pair->next_pair = current->next_pair;
                current->next_pair = new_pair;
                break;
            }
            // checking for alphabetical order
            // strcmp > 0 means key (to add) belongs above it alphabetically
            else if (strcmp(current->key, new_pair->key) > 0)
            {
                // if the current is the head of the part, make the new key the head
                if (current == partitions[partition_i]->head)
                {
                    new_pair->next_head = current;
                    partitions[partition_i]->head = new_pair;
                    break;
                }
                // otherwise put the key between
                else
                {
                    new_pair->next_head = current;
                    prev->next_head = new_pair;
                    break;
                }
            }
            // else if its not part of this group, get the next head
            else
            {
                // if were at the end, assign the next head and break
                if (current->next_head == NULL)
                {
                    current->next_head = new_pair;
                    break;
                }
                prev = current;
                current = current->next_head; // next head word in the linked list groups
            }
        }
    }
    partitions[partition_i]->size++; // increment partition size

    // unlock the partition
    pthread_mutex_unlock(&partitions[partition_i]->part_mlock);
}

/**
 * Hash a mapper's output to determine the partition that will hold it
 * Parameters:
 *    key               - Key of a specifc map output
 *    num_partitions    - Total number of partitions
 */
unsigned long MR_Partitioner(char *key, int num_partitions)
{
    unsigned long hash = 5381;
    int c;
    while ((c = *key++) != '\0')
        hash = hash * 33 + c;
    return hash % num_partitions; // partition index
}

/**
 * Run the reducer callback function for each <key, (list of values)>
 * retrieved from a partition
 * Parameters:
 *     threadarg     - Pointer to a hidden args object
 */
// threadarg will be the partition
void MR_Reduce(void *threadarg)
{
    pair *current = ((part *)threadarg)->head;
    pair *next;
    if (current == NULL)
        return;
    // char *key = (char *)malloc(strlen(current->key) + 1);
    unsigned int partition_idx = ((part *)threadarg)->part_index;
    pthread_mutex_lock(&partitions[partition_idx]->part_mlock);
    // go through all key heads, reduce the values associated to the head
    while (current != NULL)
    {
        char *key = (char *)malloc(strlen(current->key) + 1);
        strcpy(key, current->key);
        next = current->next_head;
        // call the reducer function with each partition head key
        // this will reduce (count) all keys of a groups into a single value
        Reduce(key, partition_idx);

        if (next == NULL)
        {
            free(key);
            break;
        }

        current = next; // get the next head
        free(key);
    }
    pthread_mutex_unlock(&partitions[partition_idx]->part_mlock);

    // free(key);
}

/**
 * Get the next value of the given key in the partition
 * Parameters:
 *     key           - Key of the values being reduced
 *     partition_idx - Index of the partition containing this key
 * Return:
 *     char *        - Value of the next <key, value> pair if its key is the current key
 *     NULL          - Otherwise
 */
char *MR_GetNext(char *key, unsigned int partition_idx)
{
    pair *current = partitions[partition_idx]->head, *pair;
    if (current == NULL || strcmp(current->key, key) != 0)
        return NULL;

    // if we still have the same key return the key
    if (current->next_pair != NULL)
    {
        pair = current->next_pair;
        current->next_pair = pair->next_pair;
    }
    // if we have no more pairs of the same key, get the next head
    else
    {
        pair = current; // save the current pair (group head)
        partitions[partition_idx]->head = current->next_head;
    }
    // make a copy
    char *value = (char *)malloc(strlen(pair->value) + 1);
    strcpy(value, pair->value);
    // char *value = strdup(pair->value);

    // free the pair
    free(pair->key);
    free(pair->value);
    free(pair);
    return value; // value memory gets free'd in reduce func
}