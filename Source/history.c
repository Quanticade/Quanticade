#include "attacks.h"
#include "bitboards.h"
#include "enums.h"
#include "move.h"
#include "structs.h"
#include "transposition.h"
#include "uci.h"
#include "utils.h"
#include <stdlib.h>

int QUIET_HISTORY_MALUS_MAX = 1118;
int QUIET_HISTORY_BONUS_MAX = 1246;
int QUIET_HISTORY_BASE_BONUS = 11;
int QUIET_HISTORY_FACTOR_BONUS = 172;
int QUIET_HISTORY_BASE_MALUS = 8;
int QUIET_HISTORY_FACTOR_MALUS = 217;
int QUIET_HISTORY_MAX_TT = 1270;
int QUIET_HISTORY_TT_FACTOR = 133;
int QUIET_HISTORY_TT_BASE = 65;

int CAPTURE_HISTORY_MALUS_MAX = 1050;
int CAPTURE_HISTORY_BONUS_MAX = 1379;
int CAPTURE_HISTORY_BASE_BONUS = 10;
int CAPTURE_HISTORY_FACTOR_BONUS = 176;
int CAPTURE_HISTORY_BASE_MALUS = 11;
int CAPTURE_HISTORY_FACTOR_MALUS = 235;

int CONT_HISTORY_MALUS_MAX = 1308;
int CONT_HISTORY_BONUS_MAX = 2023;
int CONT_HISTORY_BASE_BONUS = 9;
int CONT_HISTORY_FACTOR_BONUS = 177;
int CONT_HISTORY_BASE_MALUS = 9;
int CONT_HISTORY_FACTOR_MALUS = 224;

int PAWN_HISTORY_MALUS_MAX = 1138;
int PAWN_HISTORY_BONUS_MAX = 1311;
int PAWN_HISTORY_BASE_BONUS = 9;
int PAWN_HISTORY_FACTOR_BONUS = 198;
int PAWN_HISTORY_BASE_MALUS = 10;
int PAWN_HISTORY_FACTOR_MALUS = 190;

int CORR_HISTORY_MINMAX = 256;
int PAWN_CORR_HISTORY_MULTIPLIER = 40;
int NON_PAWN_CORR_HISTORY_MULTIPLIER = 30;

int FIFTY_MOVE_SCALING = 192;
int HISTORY_MAX = 8192;

uint8_t cont_hist_updates[] = {1, 2, 4};

extern keys_t keys;

uint64_t generate_pawn_key(position_t *pos) {
  // final hash key
  uint64_t final_key = 0ULL;

  // temp piece bitboard copy
  uint64_t bitboard;

  // init piece bitboard copy
  bitboard = pos->bitboards[p];

  // loop over the pieces within a bitboard
  while (bitboard) {
    // init square occupied by the piece
    int square = __builtin_ctzll(bitboard);

    // hash piece
    final_key ^= keys.piece_keys[p][square];

    // pop LS1B
    pop_bit(bitboard, square);
  }

  bitboard = pos->bitboards[P];

  // loop over the pieces within a bitboard
  while (bitboard) {
    // init square occupied by the piece
    int square = __builtin_ctzll(bitboard);

    // hash piece
    final_key ^= keys.piece_keys[P][square];

    // pop LS1B
    pop_bit(bitboard, square);
  }

  // return generated hash key
  return final_key;
}

uint64_t generate_white_non_pawn_key(position_t *pos) {
  // final hash key
  uint64_t final_key = 0ULL;

  // temp piece bitboard copy
  uint64_t bitboard;

  for (int piece = N; piece <= K; ++piece) {

    // init piece bitboard copy
    bitboard = pos->bitboards[piece];

    // loop over the pieces within a bitboard
    while (bitboard) {
      // init square occupied by the piece
      int square = __builtin_ctzll(bitboard);

      // hash piece
      final_key ^= keys.piece_keys[piece][square];

      // pop LS1B
      pop_bit(bitboard, square);
    }
  }

  // return generated hash key
  return final_key;
}

uint64_t generate_black_non_pawn_key(position_t *pos) {
  // final hash key
  uint64_t final_key = 0ULL;

  // temp piece bitboard copy
  uint64_t bitboard;

  for (int piece = n; piece <= k; ++piece) {

    // init piece bitboard copy
    bitboard = pos->bitboards[piece];

    // loop over the pieces within a bitboard
    while (bitboard) {
      // init square occupied by the piece
      int square = __builtin_ctzll(bitboard);

      // hash piece
      final_key ^= keys.piece_keys[piece][square];

      // pop LS1B
      pop_bit(bitboard, square);
    }
  }

  // return generated hash key
  return final_key;
}

int16_t calculate_corrhist_bonus(int16_t static_eval, int16_t search_score,
                                 uint8_t depth) {
  return clamp((search_score - static_eval) * depth / 8, -CORR_HISTORY_MINMAX,
               CORR_HISTORY_MINMAX);
}

int16_t scale_corrhist_bonus(int16_t score, int16_t bonus) {
  return bonus - score * abs(bonus) / 1024;
}

uint8_t static_eval_within_bounds(int16_t static_eval, int16_t score,
                                  uint8_t tt_flag) {
  const uint8_t failed_high = tt_flag == HASH_FLAG_LOWER_BOUND;
  const uint8_t failed_low = tt_flag == HASH_FLAG_UPPER_BOUND;
  return !(failed_high && static_eval >= score) &&
         !(failed_low && static_eval < score);
}

int16_t adjust_static_eval(thread_t *thread, position_t *pos,
                           int16_t static_eval) {
  const float fifty_move_scaler =
      (float)((FIFTY_MOVE_SCALING - (float)pos->fifty) / 200);
  static_eval = static_eval * fifty_move_scaler;
  const int pawn_correction =
      thread->correction_history[pos->side][pos->hash_keys.pawn_key & 16383] *
      PAWN_CORR_HISTORY_MULTIPLIER;
  const int white_non_pawn_correction =
      thread->w_non_pawn_correction_history[pos->side]
                                           [pos->hash_keys.non_pawn_key[white] &
                                            16383] *
      NON_PAWN_CORR_HISTORY_MULTIPLIER;
  const int black_non_pawn_correction =
      thread->b_non_pawn_correction_history[pos->side]
                                           [pos->hash_keys.non_pawn_key[black] &
                                            16383] *
      NON_PAWN_CORR_HISTORY_MULTIPLIER;
  const int correction =
      pawn_correction + white_non_pawn_correction + black_non_pawn_correction;
  const int adjusted_score = static_eval + (correction / 1024);
  return clamp(adjusted_score, -MATE_SCORE + 1, MATE_SCORE - 1);
}

int16_t correction_value(thread_t *thread, position_t *pos) {
  const int pawn_correction =
      thread->correction_history[pos->side][pos->hash_keys.pawn_key & 16383] *
      PAWN_CORR_HISTORY_MULTIPLIER;
  const int white_non_pawn_correction =
      thread->w_non_pawn_correction_history[pos->side]
                                           [pos->hash_keys.non_pawn_key[white] &
                                            16383] *
      NON_PAWN_CORR_HISTORY_MULTIPLIER;
  const int black_non_pawn_correction =
      thread->b_non_pawn_correction_history[pos->side]
                                           [pos->hash_keys.non_pawn_key[black] &
                                            16383] *
      NON_PAWN_CORR_HISTORY_MULTIPLIER;
  const int correction =
      pawn_correction + white_non_pawn_correction + black_non_pawn_correction;
  return correction / 1024;
}

void update_corrhist(thread_t *thread, position_t *pos, int16_t static_eval,
                     int16_t score, uint8_t depth) {
  int16_t bonus = calculate_corrhist_bonus(static_eval, score, depth);
  thread->correction_history[pos->side][pos->hash_keys.pawn_key & 16383] +=
      scale_corrhist_bonus(
          thread
              ->correction_history[pos->side][pos->hash_keys.pawn_key & 16383],
          bonus);

  thread->w_non_pawn_correction_history
      [thread->pos.side][thread->pos.hash_keys.non_pawn_key[white] & 16383] +=
      scale_corrhist_bonus(
          thread->w_non_pawn_correction_history
              [thread->pos.side]
              [thread->pos.hash_keys.non_pawn_key[white] & 16383],
          bonus);

  thread->b_non_pawn_correction_history
      [thread->pos.side][thread->pos.hash_keys.non_pawn_key[black] & 16383] +=
      scale_corrhist_bonus(
          thread->b_non_pawn_correction_history
              [thread->pos.side]
              [thread->pos.hash_keys.non_pawn_key[black] & 16383],
          bonus);
}

void update_quiet_history(thread_t *thread, position_t *pos, searchstack_t *ss,
                          int move, int bonus) {
  int target = get_move_target(move);
  int source = get_move_source(move);
  thread->quiet_history[pos->side][source][target][is_square_threatened(
      ss, source)][is_square_threatened(ss, target)] +=
      bonus -
      thread->quiet_history[pos->side][source][target][is_square_threatened(
          ss, source)][is_square_threatened(ss, target)] *
          abs(bonus) / HISTORY_MAX;
}

static inline void update_capture_history(thread_t *thread, position_t *pos,
                                          int move, int bonus) {
  int from = get_move_source(move);
  int target = get_move_target(move);
  int prev_target_piece = get_move_enpassant(move) == 0
                              ? pos->mailbox[get_move_target(move)]
                          : pos->side ? pos->mailbox[get_move_target(move) - 8]
                                      : pos->mailbox[get_move_target(move) + 8];

  thread
      ->capture_history[pos->mailbox[from]][prev_target_piece][from][target] +=
      bonus - thread->capture_history[pos->mailbox[from]][prev_target_piece]
                                     [from][target] *
                  abs(bonus) / HISTORY_MAX;
}

/*static inline void update_continuation_history(thread_t *thread,
                                               position_t *pos,
                                               searchstack_t *ss, int move,
                                               int bonus) {
  int prev_piece = ss->piece;
  int prev_target = get_move_target(ss->move);
  int piece = pos->mailbox[get_move_source(move)];
  int target = get_move_target(move);
  thread->continuation_history[prev_piece][prev_target][piece][target] +=
      bonus -
      thread->continuation_history[prev_piece][prev_target][piece][target] *
          abs(bonus) / HISTORY_MAX;
}*/

static inline void update_continuation_histories(thread_t *thread,
                                                 position_t *pos,
                                                 searchstack_t *ss, int move,
                                                 int bonus) {
  uint8_t count = sizeof(cont_hist_updates) / sizeof(uint8_t);
  for (uint8_t i = 0; i < count; ++i) {
    int prev_piece = (ss - cont_hist_updates[i])->piece;
    if (pos->ply >= cont_hist_updates[i] && prev_piece != NO_PIECE) {
      int prev_target = get_move_target((ss - cont_hist_updates[i])->move);
      int piece = pos->mailbox[get_move_source(move)];
      int target = get_move_target(move);
      thread->continuation_history[prev_piece][prev_target][piece][target] +=
          bonus -
          thread->continuation_history[prev_piece][prev_target][piece][target] *
              abs(bonus) / HISTORY_MAX;
    }
  }
}

static inline void update_pawn_history(thread_t *thread, position_t *pos,
                                       int move, int bonus) {
  int target = get_move_target(move);
  int source = get_move_source(move);
  thread->pawn_history[pos->hash_keys.pawn_key % 2048][pos->mailbox[source]]
                      [target] +=
      bonus - thread->pawn_history[pos->hash_keys.pawn_key % 2048]
                                  [pos->mailbox[source]][target] *
                  abs(bonus) / HISTORY_MAX;
}

void update_capture_history_moves(thread_t *thread, position_t *pos,
                                  moves *capture_moves, int best_move,
                                  uint8_t depth) {
  int capt_bonus =
      MIN(CAPTURE_HISTORY_BASE_BONUS + CAPTURE_HISTORY_FACTOR_BONUS * depth,
          CAPTURE_HISTORY_BONUS_MAX);
  int capt_malus =
      -MIN(CAPTURE_HISTORY_BASE_MALUS + CAPTURE_HISTORY_FACTOR_MALUS * depth,
           CAPTURE_HISTORY_MALUS_MAX);
  for (uint32_t i = 0; i < capture_moves->count; ++i) {
    if (capture_moves->entry[i].move == best_move) {
      update_capture_history(thread, pos, best_move, capt_bonus);
    } else {
      update_capture_history(thread, pos, capture_moves->entry[i].move,
                             capt_malus);
    }
  }
}

int16_t get_conthist_score(thread_t *thread, position_t *pos, searchstack_t *ss,
                           int move, uint8_t ply) {
  if (pos->ply >= ply && (ss - ply)->piece != NO_PIECE) {
    return thread->continuation_history[(ss - ply)->piece][get_move_target(
        (ss - ply)->move)][pos->mailbox[get_move_source(move)]]
                                       [get_move_target(move)];
  } else {
    return 0;
  }
}

void update_quiet_histories(thread_t *thread, position_t *pos,
                            searchstack_t *ss, moves *quiet_moves,
                            int best_move, uint8_t depth) {
  int cont_bonus =
      MIN(CONT_HISTORY_BASE_BONUS + CONT_HISTORY_FACTOR_BONUS * depth,
          CONT_HISTORY_BONUS_MAX);
  int cont_malus =
      -MIN(CONT_HISTORY_BASE_MALUS + CONT_HISTORY_FACTOR_MALUS * depth,
           CONT_HISTORY_MALUS_MAX);

  int quiet_bonus =
      MIN(QUIET_HISTORY_BASE_BONUS + QUIET_HISTORY_FACTOR_BONUS * depth,
          QUIET_HISTORY_BONUS_MAX);
  int quiet_malus =
      -MIN(QUIET_HISTORY_BASE_MALUS + QUIET_HISTORY_FACTOR_MALUS * depth,
           QUIET_HISTORY_MALUS_MAX);

  int pawn_bonus =
      MIN(PAWN_HISTORY_BASE_BONUS + PAWN_HISTORY_FACTOR_BONUS * depth,
          PAWN_HISTORY_BONUS_MAX);
  int pawn_malus =
      -MIN(PAWN_HISTORY_BASE_MALUS + PAWN_HISTORY_FACTOR_MALUS * depth,
           PAWN_HISTORY_MALUS_MAX);
  for (uint32_t i = 0; i < quiet_moves->count; ++i) {
    uint16_t move = quiet_moves->entry[i].move;
    if (move == best_move) {
      update_continuation_histories(thread, pos, ss, best_move, cont_bonus);
      update_pawn_history(thread, pos, best_move, pawn_bonus);
      update_quiet_history(thread, pos, ss, best_move, quiet_bonus);
    } else {
      update_continuation_histories(thread, pos, ss, move, cont_malus);
      update_pawn_history(thread, pos, move, pawn_malus);
      update_quiet_history(thread, pos, ss, move, quiet_malus);
    }
  }
}
