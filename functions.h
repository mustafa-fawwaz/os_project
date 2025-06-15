#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#define _GNU_SOURCE  // For strdup
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>
#include <ctype.h>

#define MAX_URL_LENGTH 2048
#define MAX_LINE_LENGTH 4096
#define MAX_WORD_LENGTH 128
#define MAX_THREADS 16
#define ALPHABET_COUNT 26

typedef struct {
    char *memory;
    size_t size;
} MemoryStruct;

typedef struct {
    char url[MAX_URL_LENGTH];
    char filename[MAX_URL_LENGTH];
} URLTask;

extern URLTask *url_tasks;
extern int total_urls;
extern int current_index;
extern int chunk_size;
extern pthread_mutex_t task_mutex;
extern pthread_mutex_t stats_mutex;
extern int global_sentence_count;
extern int global_word_count;
extern char longest_word[MAX_WORD_LENGTH];
extern int char_freq[ALPHABET_COUNT];
extern int *words_per_url;

size_t WriteMemoryCallback(void *, size_t, size_t, void *);
int fetch_url(const char *, MemoryStruct *);
void analyze_chunk(char **, int, int, int);
void process_text_in_chunks(char *, int);
int read_urls(const char *);
void update_char_frequency(const char *);
void initialize_word_tracking();
void print_word_statistics();
void *worker_thread(void *);

#endif