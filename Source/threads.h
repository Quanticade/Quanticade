#ifndef THREADS_H
#define THREADS_H

#include "structs.h"

thread_t *init_threads(int thread_count);
uint64_t total_nodes(thread_t *threads, int thread_count);

#endif