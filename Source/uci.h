#ifndef UCI_H
#define UCI_H

#include "structs.h"

#define version "Cronus 3.0"

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

extern limits_t limits;

extern const char *square_to_coordinates[];
extern char promoted_pieces[];

void generate_fen(position_t *pos, char *fen);
void uci_loop(position_t *pos, int argc, char *argv[]);
void print_move(int move);
void parse_position(position_t *pos, thread_t *thread, char *command);
void time_control(position_t *pos, thread_t *threads, char *line);

#endif
