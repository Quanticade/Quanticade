#include "movegen.h"
#include "nnue.h"
#include "search.h"
#include "structs.h"
#include "uci.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char  **fens;
    size_t  count;
} fen_book_t;

static fen_book_t load_book(const char *filename) {
    fen_book_t book = {NULL, 0};
    FILE *f = fopen(filename, "r");
    if (!f) return book;

    size_t lines = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) lines++;
    if (!lines) { fclose(f); return book; }

    book.fens = malloc(lines * sizeof(char *));
    if (!book.fens) { fclose(f); return book; }

    rewind(f);
    while (fgets(buf, sizeof(buf), f)) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
        book.fens[book.count++] = strdup(buf);
    }
    fclose(f);
    return book;
}

static void free_book(fen_book_t *book) {
    for (size_t i = 0; i < book->count; i++)
        free(book->fens[i]);
    free(book->fens);
    book->fens  = NULL;
    book->count = 0;
}

uint8_t play_rand_moves(position_t *pos, thread_t *thread, uint8_t rand_moves) {
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

    fen_book_t book = {NULL, 0};
    int use_book = bookfile && strcmp(bookfile, "None") != 0;
    if (use_book) {
        book = load_book(bookfile);
        if (!book.count) return;
    }

    int generated_fens = 0;
    while (generated_fens < n_of_fens) {
        if (use_book) {
            const char *fen = book.fens[rand() % book.count];
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

    if (use_book)
        free_book(&book);
}