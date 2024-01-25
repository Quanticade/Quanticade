#ifndef SEARCH_H
#define SEARCH_H

#include "structs.h"
void search_position(engine_t *engine, board_t *board, searchinfo_t *searchinfo,
                     tt_t *hash_table, int depth);

#endif
