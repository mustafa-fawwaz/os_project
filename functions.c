#include "functions.h"

URLTask *url_tasks = NULL;
int total_urls = 0;
int current_index = 0;
int chunk_size = 0;
pthread_mutex_t task_mutex;
int global_sentence_count = 0;
int global_word_count = 0;
char longest_word[MAX_WORD_LENGTH] = "";
pthread_mutex_t stats_mutex;
int char_freq[ALPHABET_COUNT] = {0};  // a-z character frequency
int *words_per_url = NULL;

// word freq init
WordFrequency *word_frequencies = NULL;
int word_freq_count = 0;

size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;

    char *ptr = (char *)realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "Memory allocation failed.\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

int fetch_url(const char *url, MemoryStruct *chunk) {
    CURL *curl;
    CURLcode res;

    chunk->memory = (char *)malloc(1);
    chunk->size = 0;

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "libcurl initialization failed.\n");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to fetch %s: %s\n", url, curl_easy_strerror(res));
        free(chunk->memory);
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_cleanup(curl);
    return 0;
}

void analyze_chunk(char **words, int start, int end, int url_index) {
    int sentence_count = 0, word_count = 0;

    for (int i = start; i < end; i++) {
        char *word = words[i];
        size_t len = strlen(word);

        if (len == 0)
            continue;

        if (word[len - 1] == '.' || word[len - 1] == '!' || word[len - 1] == '?') {
            sentence_count++;
        }

        word_count++;
        update_char_frequency(word);
        update_word_frequency(word);
        // call word frequency func

        pthread_mutex_lock(&stats_mutex);
        if (strlen(word) > strlen(longest_word)) {
            strncpy(longest_word, word, MAX_WORD_LENGTH - 1);
            longest_word[MAX_WORD_LENGTH - 1] = '\0';
        }
        pthread_mutex_unlock(&stats_mutex);
    }

    pthread_mutex_lock(&stats_mutex);
    global_sentence_count += sentence_count;
    global_word_count += word_count;
    words_per_url[url_index] += word_count;
    pthread_mutex_unlock(&stats_mutex);
}

void process_text_in_chunks(char *text, int url_index) {
    // Make a copy of the text since strtok modifies it
    char *text_copy = strdup(text);
    if (!text_copy) {
        fprintf(stderr, "Memory allocation failed for text copy\n");
        return;
    }
    
    char **words = (char **)malloc(sizeof(char *) * (strlen(text) / 2 + 1));
    if (!words) {
        fprintf(stderr, "Memory allocation failed for words array\n");
        free(text_copy);
        return;
    }
    
    int word_count = 0;
    char *token = strtok(text_copy, " \t\n\r");
    while (token != NULL) {
        words[word_count] = strdup(token);
        if (!words[word_count]) {
            fprintf(stderr, "Memory allocation failed for word\n");
            // Free previously allocated words
            for (int i = 0; i < word_count; i++) {
                free(words[i]);
            }
            free(words);
            free(text_copy);
            return;
        }
        word_count++;
        token = strtok(NULL, " \t\n\r");
    }

    for (int i = 0; i < word_count; i += chunk_size) {
        int end = (i + chunk_size < word_count) ? i + chunk_size : word_count;
        analyze_chunk(words, i, end, url_index);
    }

    for (int i = 0; i < word_count; i++)
        free(words[i]);
    free(words);
    free(text_copy);
}

void *worker_thread(void *arg) {
    (void)arg; // Suppress unused parameter warning
    
    while (1) {
        pthread_mutex_lock(&task_mutex);
        if (current_index >= total_urls) {
            pthread_mutex_unlock(&task_mutex);
            break;
        }

        int local_index = current_index++;
        pthread_mutex_unlock(&task_mutex);

        const char *url = url_tasks[local_index].url;
        const char *filename = url_tasks[local_index].filename;
        MemoryStruct page;

        printf("Thread %lu: Fetching %s\n", pthread_self(), url);
        if (fetch_url(url, &page) == 0) {
            FILE *fp = fopen(filename, "w");
            if (fp) {
                fwrite(page.memory, 1, page.size, fp);
                fclose(fp);
                printf("Successfully wrote %s\n", filename);
            } else {
                printf("Failed to open file %s for writing\n", filename);
            }
            
            // Only process if we have valid data
            if (page.memory && page.size > 0) {
                process_text_in_chunks(page.memory, local_index);
            }
            
            if (page.memory) {
                free(page.memory);
            }
        } else {
            printf("Failed to fetch %s\n", url);
        }
    }
    return NULL;
}

int read_urls(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open URL file");
        return -1;
    }

    char line[MAX_LINE_LENGTH];
    int count = 0;
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '\0' || line[0] == '\n')
            continue;
        count++;
    }
    rewind(file);

    url_tasks = (URLTask *)malloc(sizeof(URLTask) * count);
    if (!url_tasks) {
        fclose(file);
        return -1;
    }

    total_urls = 0;
    char url[MAX_URL_LENGTH];
    char outname[MAX_URL_LENGTH];

    while (fscanf(file, "%s %s", url, outname) == 2) {
        strncpy(url_tasks[total_urls].url, url, MAX_URL_LENGTH - 1);
        strncpy(url_tasks[total_urls].filename, outname, MAX_URL_LENGTH - 1);
        url_tasks[total_urls].url[MAX_URL_LENGTH - 1] = '\0';
        url_tasks[total_urls].filename[MAX_URL_LENGTH - 1] = '\0';
        total_urls++;
    }

    fclose(file);
    return 0;
}

void update_char_frequency(const char *word) {
    for (int i = 0; word[i] != '\0'; i++) {
        if (isalpha(word[i])) {
            char lower = tolower(word[i]);
            pthread_mutex_lock(&stats_mutex);
            char_freq[lower - 'a']++;
            pthread_mutex_unlock(&stats_mutex);
        }
    }
}

void initialize_word_tracking() {
    words_per_url = (int *)calloc(total_urls, sizeof(int));
    if (!words_per_url) {
        fprintf(stderr, "Failed to allocate memory for word stats.\n");
        exit(1);
    }
    
    word_frequencies = (WordFrequency *)malloc(MAX_WORDS * sizeof(WordFrequency));
    if (!word_frequencies) {
        fprintf(stderr, "Failed to allocate memory for word frequencies.\n");
        exit(1);
    }
    word_freq_count = 0;
}

void update_word_frequency(const char *word) {
    if (!word || strlen(word) == 0) return;
    
    pthread_mutex_lock(&stats_mutex);
    
    // Convert word to lowercase for case-insensitive comparison
    char lower_word[MAX_WORD_LENGTH];
    strncpy(lower_word, word, MAX_WORD_LENGTH - 1);
    lower_word[MAX_WORD_LENGTH - 1] = '\0';
    for (int i = 0; lower_word[i]; i++) {
        lower_word[i] = tolower(lower_word[i]);
    }
    
    // Check if word already exists
    for (int i = 0; i < word_freq_count; i++) {
        if (strcmp(word_frequencies[i].word, lower_word) == 0) {
            word_frequencies[i].frequency++;
            pthread_mutex_unlock(&stats_mutex);
            return;
        }
    }
    
    // Add new word if we haven't reached the maximum
    if (word_freq_count < MAX_WORDS) {
        strncpy(word_frequencies[word_freq_count].word, lower_word, MAX_WORD_LENGTH - 1);
        word_frequencies[word_freq_count].word[MAX_WORD_LENGTH - 1] = '\0';
        word_frequencies[word_freq_count].frequency = 1;
        word_freq_count++;
    }
    
    pthread_mutex_unlock(&stats_mutex);
}

// Comparison function for qsort
int compare_word_frequencies(const void *a, const void *b) {
    const WordFrequency *wf1 = (const WordFrequency *)a;
    const WordFrequency *wf2 = (const WordFrequency *)b;
    return wf2->frequency - wf1->frequency;  // Sort in descending order
}

void print_top_words(int count) {
    if (count > word_freq_count) {
        count = word_freq_count;
    }
    
    // Sort word frequencies
    qsort(word_frequencies, word_freq_count, sizeof(WordFrequency), compare_word_frequencies);
    
    printf("\n===== Top %d Most Frequent Words =====\n", count);
    for (int i = 0; i < count; i++) {
        printf("%d. %s: %d occurrences\n", i + 1, word_frequencies[i].word, word_frequencies[i].frequency);
    }
}

void print_word_statistics() {
    printf("\n===== Words Per URL =====\n");
    for (int i = 0; i < total_urls; i++) {
        printf("%s: %d words\n", url_tasks[i].url, words_per_url[i]);
    }

    printf("\n===== Character Frequency (a-z) =====\n");
    for (int i = 0; i < ALPHABET_COUNT; i++) {
        printf("%c: %d\n", 'a' + i, char_freq[i]);
    }
}