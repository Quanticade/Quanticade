#include <stdio.h>
#include <stdlib.h>
#include "structs.h"

thread_t *init_threads(int thread_count) {
    thread_t *threads;

#ifdef _WIN32
    threads = (thread_t *)_aligned_malloc(thread_count * sizeof(thread_t), 64);
    if (!threads) {
        fprintf(stderr, "Thread memory allocation failed.\n");
        return NULL;
    }
#else
    size_t size = thread_count * sizeof(thread_t);
    // Ensure size is a multiple of alignment
    if (size % 64 != 0) {
        size += 64 - (size % 64);
    }
    threads = (thread_t *)aligned_alloc(64, size);
    if (!threads) {
        fprintf(stderr, "Thread memory allocation failed.\n");
        return NULL;
    }
#endif

    for (int thread = 0; thread < thread_count; ++thread) {
        threads[thread].index = thread;
    }

    return threads;
}

uint64_t total_nodes(thread_t *threads, int thread_count) {
	uint64_t nodes = 0;
	for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
		nodes += threads[thread_index].nodes;
	}
	return nodes;
}

void stop_threads(thread_t *threads, int thread_count) {
	for (int i = 0; i < thread_count; ++i) {
		threads[i].stopped = 1;
	}
}
