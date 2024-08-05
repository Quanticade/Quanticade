#ifndef SEARCH_H
#define SEARCH_H

#include "structs.h"
void search_position(position_t *pos, thread_t *thread);
int SEE(position_t *pos, int move, int threshold);

#endif
