#include <stdlib.h>
#include "structs.h"

thread_t *init_threads(int thread_count) {
	thread_t *threads = aligned_alloc(64, thread_count * sizeof(thread_t));

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
