## Synchronization Primitives Used

### MapReduce Library (`mapreduce.c` and `globals.h`)
- **Mutexes (`pthread_mutex_t`)**: 
  - `part_mlock`: Used to protect access to each partition, ensuring thread-safe operations when emitting key-value pairs and during the reduce phase.

### Thread Pool Library (`threadpool.c` and `threadpool.h`)
- **Mutexes (`pthread_mutex_t`)**:
  - `queue_mlock`: Protects access to the job queue when adding or removing jobs.
  - `thread_mlock`: Protects shared variables in the thread pool, such as the count of idle threads and the `destroy` flag.
- **Condition Variables (`pthread_cond_t`)**:
  - `available_job_check`: Used by threads to wait for jobs to become available in the queue.
  - `check`: Used to signal the master thread when all jobs are completed.

## Implementation of Partitions
Partitions are implemented as an array of partition structures, where each partition represents a segment of the data to be processed:

### Structure (`part` in `globals.h`)
- **size**: The number of key-value pairs in the partition.
- **head**: A pointer to the head of a linked list containing the pairs.
- **part_mlock**: A mutex to synchronize access to the partition at the specified index.
- **part_index**: The index of the partition.

### Key-Value Pairs (`pair` in `globals.h`)
- Each partition contains a linked list of key-value pairs.
- **next_pair**: Pointer to the next pair with the same key.
- **next_head**: Pointer to the next group of pairs with a different key.
- **key** and **value**: The key-value data stored.

By using mutexes on each partition, concurrent threads can safely emit and process key-value pairs without data races.

## Testing the Implementation
The implementation was tested using the provided `distwc.c` application, which performs a distributed word count:

### Functional Testing
- Verified correct word counts on known input files.
- Tested with multiple input files of varying sizes to ensure scalability.

### Concurrency Testing
- Ran the program with different numbers of worker threads to test thread pool management.
- Used tools like **Valgrind** to check for race conditions and memory leaks.
- Used gdb to debug race conditions and identify any potential deadlocks.

### Performance Testing
- Measured execution time with varying numbers of partitions and threads.
- Ensured that synchronization primitives did not introduce significant overhead.

## Sources
- Lecture notes and slides on threading and synchronization.
