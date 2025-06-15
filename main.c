#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>
#include <ctype.h>
#include <time.h>

#include "functions.h" // Include your modular header

// Main function
int main(int argc, char *argv[])
{
    clock_t start = clock();

    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <url_file> <num_threads> <chunk_size>\n", argv[0]);
        return 1;
    }

    const char *url_file = argv[1];
    int num_threads = atoi(argv[2]);
    chunk_size = atoi(argv[3]);

    if (num_threads < 1 || num_threads > MAX_THREADS)
    {
        fprintf(stderr, "Number of threads must be between 1 and %d.\n", MAX_THREADS);
        return 1;
    }

    if (chunk_size < 1)
    {
        fprintf(stderr, "Chunk size must be at least 1.\n");
        return 1;
    }

    if (read_urls(url_file) != 0)
    {
        return 1;
    }

    initialize_word_tracking(); // Initialize per-URL word count array

    pthread_mutex_init(&task_mutex, NULL);
    pthread_mutex_init(&stats_mutex, NULL);
    curl_global_init(CURL_GLOBAL_ALL);

    pthread_t threads[num_threads];

    // Create threads
    for (int i = 0; i < num_threads; i++)
    {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

    // Join threads
    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(threads[i], NULL);
    }

    // Print final statistics
    printf("\n===== Analysis Complete =====\n");
    printf("Total Words       : %d\n", global_word_count);
    printf("Total Sentences   : %d\n", global_sentence_count);
    printf("Longest Word      : %s\n", longest_word);

    print_word_statistics(); // New function: per URL and character stats
    print_top_words(10);    // Print top 10 most frequent words

    pthread_mutex_destroy(&task_mutex);
    pthread_mutex_destroy(&stats_mutex);
    curl_global_cleanup();
    free(url_tasks);
    free(words_per_url);
    free(word_frequencies);  // Free the word frequencies array

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    printf("Execution time: %.2f seconds\n", elapsed);
    return 0;
}