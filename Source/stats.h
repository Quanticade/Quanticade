#ifndef STATS_H
#define STATS_H

#include <stdint.h>

void dbg_hit_on(int cond, int slot);
void dbg_mean_of(int64_t value, int slot);
void dbg_stdev_of(int64_t value, int slot);
void dbg_correl_of(int64_t value1, int64_t value2, int slot);
void dbg_print();

#endif