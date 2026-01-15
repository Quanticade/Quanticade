#ifndef DATAGEN_H
#define DATAGEN_H

#include "structs.h"
#include <stdint.h>

void genfens(position_t *pos, uint64_t seed, uint16_t n_of_fens);

#endif