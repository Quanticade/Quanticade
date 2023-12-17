#ifndef QUANTICADE_H
#define QUANTICADE_H

#include "structs.h"
void communicate(engine_t *engine);
uint32_t get_random_U32_number();
uint64_t get_random_uint64_number();
uint64_t generate_magic_number();
uint8_t count_bits(uint64_t bitboard);
uint8_t get_ls1b_index(uint64_t bitboard);
void init_random_keys();
uint64_t generate_hash_key(engine_t *engine);
void reset_board(engine_t *engine);
void parse_fen(engine_t *engine, char *fen);
uint64_t mask_pawn_attacks(int side, int square);
uint64_t mask_knight_attacks(int square);
uint64_t mask_king_attacks(int square);
uint64_t mask_bishop_attacks(int square);
uint64_t mask_rook_attacks(int square);
uint64_t bishop_attacks_on_the_fly(int square, uint64_t block);
uint64_t rook_attacks_on_the_fly(int square, uint64_t block);
void init_leapers_attacks();
uint64_t set_occupancy(int index, int bits_in_mask, uint64_t attack_mask);
uint64_t find_magic_number(int square, int relevant_bits, int bishop);
void init_magic_numbers();
void init_sliders_attacks(int bishop);
uint64_t get_bishop_attacks(int square, uint64_t occupancy);
uint64_t get_rook_attacks(int square, uint64_t occupancy);
uint64_t get_queen_attacks(int square, uint64_t occupancy);
int is_square_attacked(engine_t *engine, int square, int side);
void add_move(moves *move_list, int move);
void print_move(int move);
void print_move_list(moves *move_list);
int make_move(engine_t *engine, int move, int move_flag);
void generate_moves(engine_t *engine, moves *move_list);
void perft_driver(engine_t *engine, int depth);
void perft_test(engine_t *engine, int depth);
uint64_t set_file_rank_mask(int file_number, int rank_number);
void init_evaluation_masks();
int get_game_phase_score(engine_t *engine);
int evaluate(engine_t *engine);
void clear_hash_table();
void init_hash_table(int mb);
int read_hash_entry(engine_t *engine, int alpha, int beta,
                                  int depth);
void write_hash_entry(engine_t *engine, int score, int depth,
                                    int hash_flag);
void enable_pv_scoring(engine_t *engine, moves *move_list);
int score_move(engine_t *engine, int move);
void sort_moves(engine_t *engine, moves *move_list);
void print_move_scores(engine_t *engine, moves *move_list);
int is_repetition(engine_t *engine);
int quiescence(engine_t *engine, int alpha, int beta);
int negamax(engine_t *engine, int alpha, int beta, int depth);
void search_position(engine_t *engine, int depth);
void reset_time_control(engine_t *engine);
void init_all();

#endif
