#include <stdio.h>     // Standard I/O functions (printf, fprintf, fopen, fclose, fwrite)
#include <string.h>    // String manipulation functions (strncpy)
#include <stdlib.h>    // Memory allocation functions (malloc, free, realloc)
#include <pthread.h>   // POSIX threads (pthread_create, pthread_join, mutex functions)
#include <curl/curl.h> // libcurl functions for HTTP requests

#define MAX_THREADS 16      // Maximum number of threads allowed
#define MAX_URL_LENGTH 2048 // Maximum length for URL strings

// Structure to hold download task information
typedef struct
{
    char url[MAX_URL_LENGTH];      // URL to download
    char filename[MAX_URL_LENGTH]; // Local filename to save to
} DownloadTask;

// Structure to manage the task queue
typedef struct
{
    DownloadTask *tasks;  // Pointer to array of tasks
    int count;            // Total number of tasks
    int index;            // Current task index
    pthread_mutex_t lock; // Mutex for thread-safe access
} TaskQueue;

// Structure to hold downloaded data in memory
struct MemoryStruct
{
    char *memory; // Pointer to downloaded data
    size_t size;  // Size of downloaded data
};

/**
 * Initializes the task queue
 * @param queue Pointer to TaskQueue structure
 * @param tasks Array of DownloadTask items
 * @param count Number of tasks in the array
 *
 * Built-in functions used:
 * - pthread_mutex_init: Initializes a mutex for thread synchronization
 */
void init_queue(TaskQueue *queue, DownloadTask *tasks, int count)
{
    queue->tasks = tasks;
    queue->count = count;
    queue->index = 0;
    pthread_mutex_init(&queue->lock, NULL); // Initialize mutex with default attributes
    printf("Queue initialized with %d tasks\n", count);
}

/**
 * Gets the next task from the queue (thread-safe)
 * @param queue Pointer to TaskQueue structure
 * @param task Pointer to store the retrieved task
 * @return 1 if task retrieved, 0 if no more tasks
 *
 * Built-in functions used:
 * - pthread_mutex_lock: Locks the mutex to prevent concurrent access
 * - pthread_mutex_unlock: Releases the mutex after critical section
 */
int get_next_task(TaskQueue *queue, DownloadTask *task)
{
    pthread_mutex_lock(&queue->lock); // Enter critical section

    if (queue->index >= queue->count)
    {
        pthread_mutex_unlock(&queue->lock);
        return 0; // No more tasks
    }

    *task = queue->tasks[queue->index++];
    printf("Task %d assigned (total %d)\n", queue->index, queue->count);

    pthread_mutex_unlock(&queue->lock); // Exit critical section
    return 1;
}

/**
 * Callback function for libcurl to write received data
 * @param contents Pointer to received data
 * @param size Size of each data element
 * @param nmemb Number of elements
 * @param userp Pointer to user data (MemoryStruct)
 * @return Number of bytes actually written
 *
 * Built-in functions used:
 * - realloc: Resizes memory block while preserving contents
 * - memcpy: Copies memory from source to destination
 */
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    // Resize memory block to accommodate new data
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr)
    {
        fprintf(stderr, "Memory error\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize); // Copy new data
    mem->size += realsize;
    mem->memory[mem->size] = 0; // Null-terminate the string
    return realsize;
}

/**
 * Downloads a URL and saves to file
 * @param url URL to download
 * @param filename Local filename to save to
 * @return 0 on success, -1 on failure
 *
 * Built-in functions used:
 * - malloc: Allocates memory
 * - free: Releases allocated memory
 * - fopen: Opens a file
 * - fwrite: Writes data to file
 * - fclose: Closes a file
 * - curl_easy_init: Initializes a libcurl easy handle
 * - curl_easy_setopt: Sets options for libcurl transfer
 * - curl_easy_perform: Performs the file transfer
 * - curl_easy_cleanup: Cleans up libcurl easy handle
 */
int download_url(const char *url, const char *filename)
{
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk = {0};

    chunk.memory = malloc(1); // Allocate initial memory
    if (!chunk.memory)
        return -1;

    curl = curl_easy_init();
    if (curl)
    {
        // Set libcurl options
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);       // 30-second timeout

        res = curl_easy_perform(curl); // Perform the download
        if (res != CURLE_OK)
        {
            fprintf(stderr, "Download failed: %s\n", curl_easy_strerror(res));
            free(chunk.memory);
            curl_easy_cleanup(curl);
            return -1;
        }

        // Save to file
        FILE *fp = fopen(filename, "wb"); // Open in binary mode
        if (fp)
        {
            fwrite(chunk.memory, 1, chunk.size, fp); // Write data to file
            fclose(fp);                              // Close file
        }

        free(chunk.memory);      // Free downloaded data
        curl_easy_cleanup(curl); // Cleanup libcurl handle
        return 0;
    }
    return -1;
}

/**
 * Thread function to process download tasks
 * @param arg Pointer to TaskQueue structure
 * @return NULL
 *
 * Built-in functions used:
 * - pthread_self: Returns the thread ID of calling thread
 */
void *download_thread(void *arg)
{
    TaskQueue *queue = (TaskQueue *)arg;
    DownloadTask task;

    printf("Thread %lu started\n", (unsigned long)pthread_self());

    // Process tasks until queue is empty
    while (get_next_task(queue, &task))
    {
        printf("Thread %lu processing: %s\n",
               (unsigned long)pthread_self(), task.url);

        if (download_url(task.url, task.filename) == 0)
        {
            printf("Thread %lu completed: %s\n",
                   (unsigned long)pthread_self(), task.url);
        }
        else
        {
            fprintf(stderr, "Thread %lu failed: %s\n",
                    (unsigned long)pthread_self(), task.url);
        }
    }

    printf("Thread %lu exiting\n", (unsigned long)pthread_self());
    return NULL;
}

/**
 * Main program entry point
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 *
 * Built-in functions used:
 * - atoi: Converts string to integer
 * - strncpy: Copies string with length limit
 * - pthread_create: Creates a new thread
 * - pthread_join: Waits for thread termination
 * - curl_global_init: Initializes libcurl globally
 * - curl_global_cleanup: Cleans up libcurl
 */
int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        printf("Usage: %s <thread_count> <url1> <output1> [url2 output2...]\n", argv[0]);
        return 1;
    }

    // Parse thread count
    int thread_count = atoi(argv[1]);
    if (thread_count < 1 || thread_count > MAX_THREADS)
    {
        fprintf(stderr, "Invalid thread count (1-%d)\n", MAX_THREADS);
        return 1;
    }

    // Calculate number of tasks
    int task_count = (argc - 2) / 2;
    if (task_count < 1)
    {
        fprintf(stderr, "No tasks specified\n");
        return 1;
    }

    // Prepare task array
    DownloadTask tasks[task_count];
    for (int i = 0; i < task_count; i++)
    {
        strncpy(tasks[i].url, argv[2 + i * 2], MAX_URL_LENGTH - 1);
        strncpy(tasks[i].filename, argv[3 + i * 2], MAX_URL_LENGTH - 1);
        tasks[i].url[MAX_URL_LENGTH - 1] = '\0'; // Ensure null-termination
        tasks[i].filename[MAX_URL_LENGTH - 1] = '\0';
    }

    // Initialize task queue
    TaskQueue queue;
    init_queue(&queue, tasks, task_count);

    // Initialize libcurl (must be done before creating threads)
    curl_global_init(CURL_GLOBAL_ALL);

    printf("Creating %d threads for %d tasks\n", thread_count, task_count);
    pthread_t threads[thread_count];

    // Create worker threads
    for (int i = 0; i < thread_count; i++)
    {
        if (pthread_create(&threads[i], NULL, download_thread, &queue) != 0)
        {
            fprintf(stderr, "Error creating thread %d\n", i);
            curl_global_cleanup();
            return 1;
        }
    }

    // Wait for all threads to complete
    for (int i = 0; i < thread_count; i++)
    {
        pthread_join(threads[i], NULL);
    }

    // Cleanup libcurl
    curl_global_cleanup();
    printf("All downloads completed\n");
    return 0;
}