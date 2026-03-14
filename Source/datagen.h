#ifndef DATAGEN_H
#define DATAGEN_H

#include "structs.h"
#include <stdint.h>

void genfens(position_t *pos, thread_t *thread, uint64_t seed,
             uint16_t n_of_fens, const char *bookfile);

#endif
