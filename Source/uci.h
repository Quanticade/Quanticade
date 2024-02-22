#ifndef UCI_H
#define UCI_H

#include "structs.h"

#define version "0.5 Dev"

extern const char *square_to_coordinates[];
extern char promoted_pieces[];

void uci_loop(engine_t *engine, position_t *pos, searchinfo_t *searchinfo);
void print_move(int move);

#endif
