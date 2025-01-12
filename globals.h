#include <stdlib.h>

typedef struct pair{
    struct pair *next_pair; // ptr to next pair of the same key
    struct pair *next_head; // ptr to the next head group
    char *key;
    char *value;
}pair;

typedef struct part{
    unsigned int size; // size of the partition
    pair *head;  // ptr to the top of partition linked list
    pthread_mutex_t part_mlock; // lock for each partition
    unsigned int part_index; // index of the partition
}part;