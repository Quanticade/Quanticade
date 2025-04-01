#ifndef UCI_H
#define UCI_H

#include "structs.h"

#define version "Iocus 1.0"

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

extern limits_t limits;

extern const char *square_to_coordinates[];
extern char promoted_pieces[];

void uci_loop(position_t *pos, thread_t *threads, int argc, char *argv[]);
void print_move(int move);

#endif
