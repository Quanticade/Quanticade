
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <inttypes.h>

typedef struct {
    atomic_int_fast64_t data[6];
} DebugInfo;

    #define MAX_DEBUG_SLOTS 32

DebugInfo hit[MAX_DEBUG_SLOTS];
DebugInfo mean[MAX_DEBUG_SLOTS];
DebugInfo stdev[MAX_DEBUG_SLOTS];
DebugInfo correl[MAX_DEBUG_SLOTS];

void dbg_hit_on(int cond, int slot) {
    atomic_fetch_add(&hit[slot].data[0], 1);
    if (cond)
    {
        atomic_fetch_add(&hit[slot].data[1], 1);
    }
}

void dbg_mean_of(int64_t value, int slot) {
    atomic_fetch_add(&mean[slot].data[0], 1);
    atomic_fetch_add(&mean[slot].data[1], value);
}

void dbg_stdev_of(int64_t value, int slot) {
    atomic_fetch_add(&stdev[slot].data[0], 1);
    atomic_fetch_add(&stdev[slot].data[1], value);
    atomic_fetch_add(&stdev[slot].data[2], value * value);
}

void dbg_correl_of(int64_t value1, int64_t value2, int slot) {
    atomic_fetch_add(&correl[slot].data[0], 1);
    atomic_fetch_add(&correl[slot].data[1], value1);
    atomic_fetch_add(&correl[slot].data[2], value1 * value1);
    atomic_fetch_add(&correl[slot].data[3], value2);
    atomic_fetch_add(&correl[slot].data[4], value2 * value2);
    atomic_fetch_add(&correl[slot].data[5], value1 * value2);
}

void dbg_print(void) {
    int64_t n;
    for (int i = 0; i < MAX_DEBUG_SLOTS; ++i)
    {
        if ((n = atomic_load(&hit[i].data[0])))
        {
            double hitRate = 100.0 * (double) atomic_load(&hit[i].data[1]) / n;
            fprintf(stderr, "Hit #%d: Total %" PRIu64 " Hits %" PRIu64 " Hit Rate (%%) %.2f\n", i, n,
                    atomic_load(&hit[i].data[1]), hitRate);
        }
    }

    for (int i = 0; i < MAX_DEBUG_SLOTS; ++i)
    {
        if ((n = atomic_load(&mean[i].data[0])))
        {
            double meanValue = (double) atomic_load(&mean[i].data[1]) / n;
            fprintf(stderr, "Mean #%d: Total %" PRIu64 " Mean %.2f\n", i, n, meanValue);
        }
    }

    for (int i = 0; i < MAX_DEBUG_SLOTS; ++i)
    {
        if ((n = atomic_load(&stdev[i].data[0])))
        {
            double meanValue = (double) atomic_load(&stdev[i].data[1]) / n;
            double variance =
              ((double) atomic_load(&stdev[i].data[2]) / n) - (meanValue * meanValue);
            double stddev = sqrt(variance);
            fprintf(stderr, "Stdev #%d: Total %" PRIu64 " Stdev %.2f\n", i, n, stddev);
        }
    }

    for (int i = 0; i < MAX_DEBUG_SLOTS; ++i)
    {
        if ((n = atomic_load(&correl[i].data[0])))
        {
            double meanX       = (double) atomic_load(&correl[i].data[1]) / n;
            double meanXX      = (double) atomic_load(&correl[i].data[2]) / n;
            double meanY       = (double) atomic_load(&correl[i].data[3]) / n;
            double meanYY      = (double) atomic_load(&correl[i].data[4]) / n;
            double meanXY      = (double) atomic_load(&correl[i].data[5]) / n;
            double numerator   = meanXY - meanX * meanY;
            double denominator = sqrt((meanXX - meanX * meanX) * (meanYY - meanY * meanY));
            double correlation = numerator / denominator;
            fprintf(stderr, "Correl. #%d: Total %" PRIu64 " Coefficient %.2f\n", i, n, correlation);
        }
    }
}
