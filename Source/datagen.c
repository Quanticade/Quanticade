#include "movegen.h"
#include "nnue.h"
#include "search.h"
#include "structs.h"
#include "uci.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int random_fen_from_file(const char *filename, char *out_fen, size_t max_len) {
    FILE *f = fopen(filename, "r");
    if (!f) return 0;

    char buffer[256];
    uint64_t count = 0;

    while (fgets(buffer, sizeof(buffer), f)) {
        count++;

        if (rand() % count == 0) {
            strncpy(out_fen, buffer, max_len - 1);
            out_fen[max_len - 1] = '\0';

            // remove newline
            char *nl = strchr(out_fen, '\n');
            if (nl) *nl = '\0';
        }
    }

    fclose(f);

    return count > 0;
}

uint8_t play_rand_moves(position_t *pos, thread_t * thread, uint8_t rand_moves) {
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
        char input[999];
        strcpy(input, "position fen ");
        strcat(input, fen);
        parse_position(pos, thread, input);
        init_accumulator(pos, &thread->accumulator[pos->ply]);
        init_finny_tables(thread, pos);
        time_control(pos, thread, "go depth 10");
        search_position(pos, thread);
        if (abs(thread->score) > 1000) {
            return 0;
        }
        printf("info string genfens %s\n", fen);
        return 1;
    }

    uint16_t move = legal_moves->entry[rand() % legal_moves->count].move;
    position_t pos_copy = *pos;
    make_move(&pos_copy, move);
    return play_rand_moves(&pos_copy, thread, rand_moves - 1);
}

void genfens(position_t *pos, thread_t *thread, uint64_t seed,
             uint16_t n_of_fens, const char *bookfile) {

    srand(seed);

    int generated_fens = 0;

    while (generated_fens < n_of_fens) {

        if (bookfile != NULL && strcmp(bookfile, "None") != 0) {
            char fen[256];

            if (!random_fen_from_file(bookfile, fen, sizeof(fen)))
                return;

            char input[512];
            sprintf(input, "position fen %s", fen);
            parse_position(pos, thread, input);
            init_accumulator(pos, &thread->accumulator[pos->ply]);
            init_finny_tables(thread, pos);
        }

        position_t pos_copy = *pos;

        int random_moves = 6 + rand() % 4;

        if (play_rand_moves(&pos_copy, thread, random_moves))
            generated_fens++;
    }
}
