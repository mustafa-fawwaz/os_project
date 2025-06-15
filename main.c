#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>
#include <ctype.h>
#include <time.h>

#define MAX_URL_LENGTH 2048
#define MAX_LINE_LENGTH 4096
#define MAX_THREADS 16
#define MAX_WORD_LENGTH 128

// Structure to hold a downloaded web page in memory
struct MemoryStruct
{
    char *memory;
    size_t size;
};

// Structure to hold URL and associated data
typedef struct
{
    char url[MAX_URL_LENGTH];
    char filename[MAX_URL_LENGTH];
} URLTask;

// Global array of URLs and thread-safe index
URLTask *url_tasks = NULL;
int total_urls = 0;
int current_index = 0;
int chunk_size = 0;
pthread_mutex_t task_mutex;

// Statistics (shared across threads)
int global_sentence_count = 0;
int global_word_count = 0;
char longest_word[MAX_WORD_LENGTH] = "";
pthread_mutex_t stats_mutex;

// Helper to write data fetched via libcurl
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr)
    {
        fprintf(stderr, "Memory allocation failed.\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

// Download content from a URL using libcurl
int fetch_url(const char *url, struct MemoryStruct *chunk)
{
    CURL *curl;
    CURLcode res;

    chunk->memory = malloc(1);
    chunk->size = 0;

    curl = curl_easy_init();
    if (!curl)
    {
        fprintf(stderr, "libcurl initialization failed.\n");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        fprintf(stderr, "Failed to fetch %s: %s\n", url, curl_easy_strerror(res));
        free(chunk->memory);
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_cleanup(curl);
    return 0;
}

// Analyze a chunk of words and update statistics
void analyze_chunk(char **words, int start, int end)
{
    int sentence_count = 0, word_count = 0;

    for (int i = start; i < end; i++)
    {
        char *word = words[i];
        size_t len = strlen(word);

        if (len == 0)
            continue;

        if (word[len - 1] == '.' || word[len - 1] == '!' || word[len - 1] == '?')
        {
            sentence_count++;
        }

        word_count++;

        pthread_mutex_lock(&stats_mutex);
        if (strlen(word) > strlen(longest_word))
        {
            strncpy(longest_word, word, MAX_WORD_LENGTH);
        }
        pthread_mutex_unlock(&stats_mutex);
    }

    pthread_mutex_lock(&stats_mutex);
    global_sentence_count += sentence_count;
    global_word_count += word_count;
    pthread_mutex_unlock(&stats_mutex);
}

// Tokenize full text into words and process in chunks
void process_text_in_chunks(char *text)
{
    char **words = malloc(sizeof(char *) * (strlen(text) / 2 + 1));
    int word_count = 0;

    char *token = strtok(text, " \t\n\r");
    while (token != NULL)
    {
        words[word_count] = strdup(token);
        word_count++;
        token = strtok(NULL, " \t\n\r");
    }

    for (int i = 0; i < word_count; i += chunk_size)
    {
        int end = (i + chunk_size < word_count) ? i + chunk_size : word_count;
        analyze_chunk(words, i, end);
    }

    for (int i = 0; i < word_count; i++)
        free(words[i]);
    free(words);
}

// Worker thread function
void *worker_thread(void *arg)
{
    while (1)
    {
        pthread_mutex_lock(&task_mutex);
        if (current_index >= total_urls)
        {
            pthread_mutex_unlock(&task_mutex);
            break;
        }

        int local_index = current_index++;
        pthread_mutex_unlock(&task_mutex);

        const char *url = url_tasks[local_index].url;
        const char *filename = url_tasks[local_index].filename;
        struct MemoryStruct page;

        printf("Thread %lu: Fetching %s\n", pthread_self(), url);
        if (fetch_url(url, &page) == 0)
        {
            FILE *fp = fopen(filename, "w");
            if (fp)
            {
                fwrite(page.memory, 1, page.size, fp);
                fclose(fp);
            }
            process_text_in_chunks(page.memory);
            free(page.memory);
        }
    }
    return NULL;
}

// Read URLs from a file
int read_urls(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        perror("Failed to open URL file");
        return -1;
    }

    char line[MAX_LINE_LENGTH];
    int count = 0;
    while (fgets(line, sizeof(line), file))
    {
        if (line[0] == '\0' || line[0] == '\n')
            continue;
        count++;
    }
    rewind(file);

    url_tasks = malloc(sizeof(URLTask) * count);
    if (!url_tasks)
    {
        fclose(file);
        return -1;
    }

    total_urls = 0;
    char url[MAX_URL_LENGTH];
    char outname[MAX_URL_LENGTH];

    while (fscanf(file, "%s %s", url, outname) == 2)
    {
        strncpy(url_tasks[total_urls].url, url, MAX_URL_LENGTH - 1);
        strncpy(url_tasks[total_urls].filename, outname, MAX_URL_LENGTH - 1);
        url_tasks[total_urls].url[MAX_URL_LENGTH - 1] = '\0';
        url_tasks[total_urls].filename[MAX_URL_LENGTH - 1] = '\0';
        total_urls++;
    }

    fclose(file);
    return 0;
}

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

    pthread_mutex_destroy(&task_mutex);
    pthread_mutex_destroy(&stats_mutex);
    curl_global_cleanup();
    free(url_tasks);

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    printf("Execution time: %.2f seconds\n", elapsed);
    return 0;
}
