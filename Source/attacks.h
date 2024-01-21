#ifndef ATTACKS_H
#define ATTACKS_H

#include "structs.h"
#include <stdint.h>

uint64_t get_bishop_attacks(engine_t *engine, int square, uint64_t occupancy);
uint64_t get_rook_attacks(engine_t *engine, int square, uint64_t occupancy);
uint64_t get_queen_attacks(engine_t *engine, int square, uint64_t occupancy);
int is_square_attacked(engine_t *engine, int square, int side);
void init_sliders_attacks(engine_t *engine, int bishop);
void init_leapers_attacks(engine_t *engine);

#endif
