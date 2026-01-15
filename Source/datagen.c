#include "movegen.h"
#include "structs.h"
#include "uci.h"
#include <stdio.h>
#include <stdlib.h>

uint8_t play_rand_moves(position_t *pos, uint8_t rand_moves) {
    moves pseudo_moves[1];
    moves legal_moves[1];
    legal_moves->count = 0;
    generate_moves(pos, pseudo_moves);
    for (uint16_t moves = 0; moves < pseudo_moves->count; ++moves) {
        if (is_pseudo_legal(pos, pseudo_moves->entry[moves].move) && is_legal(pos, pseudo_moves->entry[moves].move)) {
            add_move(legal_moves, pseudo_moves->entry[moves].move);
        }
    }

    if (legal_moves->count == 0) {
        return 0;
    }

    if (rand_moves == 0) {
        char fen[99];
        generate_fen(pos, fen);
        printf("info string genfens %s\n", fen);
        return 1;
    }

    uint16_t move = legal_moves->entry[rand() % legal_moves->count].move;
    position_t pos_copy = *pos;
    make_move(&pos_copy, move);
    return play_rand_moves(&pos_copy, rand_moves - 1);
}

void genfens(position_t *pos, uint64_t seed, uint16_t n_of_fens) {
    srand(seed);

    int generated_fens = 0;
    while (generated_fens < n_of_fens) {
        position_t pos_copy = *pos;
        // 6-9 random moves from startpos
        int random_moves = 6 + rand() % 4;
        if (play_rand_moves(&pos_copy, random_moves)) {
            generated_fens++;
        }
    }
}
