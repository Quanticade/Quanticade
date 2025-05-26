#include "bitboards.h"
#include "enums.h"
#include "move.h"
#include "structs.h"
#include "transposition.h"
#include "uci.h"
#include "utils.h"
#include <stdlib.h>

int QUIET_HISTORY_MALUS_MAX = 1127;
int QUIET_HISTORY_BONUS_MAX = 1351;
int QUIET_HISTORY_BASE_BONUS = 10;
int QUIET_HISTORY_FACTOR_BONUS = 200;
int QUIET_HISTORY_BASE_MALUS = 10;
int QUIET_HISTORY_FACTOR_MALUS = 200;

int CAPTURE_HISTORY_MALUS_MAX = 1042;
int CAPTURE_HISTORY_BONUS_MAX = 1386;
int CAPTURE_HISTORY_BASE_BONUS = 10;
int CAPTURE_HISTORY_FACTOR_BONUS = 200;
int CAPTURE_HISTORY_BASE_MALUS = 10;
int CAPTURE_HISTORY_FACTOR_MALUS = 200;

int CONT_HISTORY_MALUS_MAX = 1448;
int CONT_HISTORY_BONUS_MAX = 1531;
int CONT_HISTORY_BASE_BONUS = 10;
int CONT_HISTORY_FACTOR_BONUS = 200;
int CONT_HISTORY_BASE_MALUS = 10;
int CONT_HISTORY_FACTOR_MALUS = 200;
int CONT_HISTORY_BASE2_BONUS = 10;
int CONT_HISTORY_FACTOR2_BONUS = 200;
int CONT_HISTORY_BASE2_MALUS = 10;
int CONT_HISTORY_FACTOR2_MALUS = 200;
int CONT_HISTORY_BASE4_BONUS = 10;
int CONT_HISTORY_FACTOR4_BONUS = 200;
int CONT_HISTORY_BASE4_MALUS = 10;
int CONT_HISTORY_FACTOR4_MALUS = 200;

int PAWN_HISTORY_MALUS_MAX = 1127;
int PAWN_HISTORY_BONUS_MAX = 1351;
int PAWN_HISTORY_BASE_BONUS = 10;
int PAWN_HISTORY_FACTOR_BONUS = 200;
int PAWN_HISTORY_BASE_MALUS = 10;
int PAWN_HISTORY_FACTOR_MALUS = 200;

int CORR_HISTORY_MINMAX = 136;
int PAWN_CORR_HISTORY_MULTIPLIER = 22;
int HISTORY_MAX = 8192;

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
  const int pawn_correction =
      thread->correction_history[pos->side][pos->hash_keys.pawn_key & 16383] *
      PAWN_CORR_HISTORY_MULTIPLIER;
  const int adjusted_score = static_eval + pawn_correction / 1024;
  // printf("%d %d %d\n", pawn_correction / 1024, adjusted_score, static_eval);
  return clamp(adjusted_score, -MATE_SCORE + 1, MATE_SCORE - 1);
}

void update_pawn_corrhist(thread_t *thread, int16_t static_eval, int16_t score,
                          uint8_t depth, uint8_t tt_flag) {
  if (!static_eval_within_bounds(static_eval, score, tt_flag)) {
    return;
  }
  int16_t bonus = calculate_corrhist_bonus(static_eval, score, depth);
  thread->correction_history[thread->pos.side]
                            [thread->pos.hash_keys.pawn_key & 16383] +=
      scale_corrhist_bonus(
          thread->correction_history[thread->pos.side]
                                    [thread->pos.hash_keys.pawn_key & 16383],
          bonus);
}

static inline void update_quiet_history(thread_t *thread, int move, int bonus) {
  int target = get_move_target(move);
  int source = get_move_source(move);
  thread->quiet_history[thread->pos.mailbox[source]][source][target] +=
      bonus -
      thread->quiet_history[thread->pos.mailbox[source]][source][target] *
          abs(bonus) / HISTORY_MAX;
}

static inline void update_capture_history(thread_t *thread, int move,
                                          int bonus) {
  int from = get_move_source(move);
  int target = get_move_target(move);
  thread->capture_history[thread->pos.mailbox[from]]
                         [thread->pos.mailbox[target]][from][target] +=
      bonus -
      thread->capture_history[thread->pos.mailbox[from]]
                             [thread->pos.mailbox[target]][from][target] *
          abs(bonus) / HISTORY_MAX;
}

void update_continuation_history(thread_t *thread,
                                               searchstack_t *ss, int move,
                                               int bonus) {
  int prev_piece = ss->piece;
  int prev_target = get_move_target(ss->move);
  int piece = thread->pos.mailbox[get_move_source(move)];
  int target = get_move_target(move);
  thread->continuation_history[prev_piece][prev_target][piece][target] +=
      bonus -
      thread->continuation_history[prev_piece][prev_target][piece][target] *
          abs(bonus) / HISTORY_MAX;
}

static inline void update_pawn_history(thread_t *thread, int move, int bonus) {
  int target = get_move_target(move);
  int source = get_move_source(move);
  thread->pawn_history[thread->pos.hash_keys.pawn_key % 32767]
                      [thread->pos.mailbox[source]][target] +=
      bonus - thread->pawn_history[thread->pos.hash_keys.pawn_key % 32767]
                                  [thread->pos.mailbox[source]][target] *
                  abs(bonus) / HISTORY_MAX;
}

void update_capture_history_moves(thread_t *thread, moves *capture_moves,
                                  int best_move, uint8_t depth) {
  int capt_bonus =
      MIN(CAPTURE_HISTORY_BASE_BONUS + CAPTURE_HISTORY_FACTOR_BONUS * depth,
          CAPTURE_HISTORY_BONUS_MAX);
  int capt_malus =
      -MIN(CAPTURE_HISTORY_BASE_MALUS + CAPTURE_HISTORY_FACTOR_MALUS * depth,
           CAPTURE_HISTORY_MALUS_MAX);
  for (uint32_t i = 0; i < capture_moves->count; ++i) {
    if (capture_moves->entry[i].move == best_move) {
      update_capture_history(thread, best_move, capt_bonus);
    } else {
      update_capture_history(thread, capture_moves->entry[i].move, capt_malus);
    }
  }
}

int16_t get_conthist_score(thread_t *thread, searchstack_t *ss, int move) {
  return thread->continuation_history[ss->piece][get_move_target(
      ss->move)][thread->pos.mailbox[get_move_source(move)]]
                                     [get_move_target(move)];
}

void update_quiet_histories(thread_t *thread, searchstack_t *ss,
                            moves *quiet_moves, int best_move, uint8_t depth) {
  int cont_bonus =
      MIN(CONT_HISTORY_BASE_BONUS + CONT_HISTORY_FACTOR_BONUS * depth,
          CONT_HISTORY_BONUS_MAX);
  int cont_malus =
      -MIN(CONT_HISTORY_BASE_MALUS + CONT_HISTORY_FACTOR_MALUS * depth,
           CONT_HISTORY_MALUS_MAX);
  int cont_bonus2 =
      MIN(CONT_HISTORY_BASE2_BONUS + CONT_HISTORY_FACTOR2_BONUS * depth,
          CONT_HISTORY_BONUS_MAX);
  int cont_malus2 =
      -MIN(CONT_HISTORY_BASE2_MALUS + CONT_HISTORY_FACTOR2_MALUS * depth,
           CONT_HISTORY_MALUS_MAX);
  int cont_bonus4 =
      MIN(CONT_HISTORY_BASE4_BONUS + CONT_HISTORY_FACTOR4_BONUS * depth,
          CONT_HISTORY_BONUS_MAX);
  int cont_malus4 =
      -MIN(CONT_HISTORY_BASE4_MALUS + CONT_HISTORY_FACTOR4_MALUS * depth,
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
      update_continuation_history(thread, ss - 1, best_move, cont_bonus);
      update_continuation_history(thread, ss - 2, best_move, cont_bonus2);
      update_continuation_history(thread, ss - 4, best_move, cont_bonus4);
      update_pawn_history(thread, best_move, pawn_bonus);
      update_quiet_history(thread, best_move, quiet_bonus);
    } else {
      update_continuation_history(thread, ss - 1, move, cont_malus);
      update_continuation_history(thread, ss - 2, move, cont_malus2);
      update_continuation_history(thread, ss - 4, move, cont_malus4);
      update_pawn_history(thread, move, pawn_malus);
      update_quiet_history(thread, move, quiet_malus);
    }
  }
}
