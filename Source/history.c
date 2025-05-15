#include "bitboards.h"
#include "enums.h"
#include "move.h"
#include "structs.h"
#include "transposition.h"
#include "utils.h"
#include <stdlib.h>

int CAPTURE_HISTORY_BONUS_MAX = 1386;
int QUIET_HISTORY_BONUS_MAX = 1351;
int CONT_HISTORY_BONUS_MAX = 1531;
int CAPTURE_HISTORY_MALUS_MAX = 1042;
int QUIET_HISTORY_MALUS_MAX = 1127;
int CONT_HISTORY_MALUS_MAX = 1448;
int CAPTURE_HISTORY_BONUS_MIN = 1544;
int QUIET_HISTORY_BONUS_MIN = 1512;
int CONT_HISTORY_BONUS_MIN = 1470;
int CAPTURE_HISTORY_MALUS_MIN = 1350;
int QUIET_HISTORY_MALUS_MIN = 1417;
int CONT_HISTORY_MALUS_MIN = 1378;
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

static inline void update_quiet_history(thread_t *thread, int move,
                                        uint8_t depth, uint8_t is_best_move) {
  int target = get_move_target(move);
  int source = get_move_source(move);
  int bonus = 16 * depth * depth + 32 * depth + 16;
  int clamped_bonus =
      clamp(bonus, -QUIET_HISTORY_BONUS_MIN, QUIET_HISTORY_BONUS_MAX);
  int clamped_malus =
      clamp(bonus, -QUIET_HISTORY_MALUS_MIN, QUIET_HISTORY_MALUS_MAX);
  int adjust = is_best_move ? clamped_bonus : -clamped_malus;
  thread->quiet_history[thread->pos.mailbox[source]][source][target] +=
      adjust -
      thread->quiet_history[thread->pos.mailbox[source]][source][target] *
          abs(adjust) / HISTORY_MAX;
}

static inline void update_capture_history(thread_t *thread, int move,
                                          uint8_t depth, uint8_t is_best_move) {
  int from = get_move_source(move);
  int target = get_move_target(move);
  int bonus = 16 * depth * depth + 32 * depth + 16;
  int clamped_bonus =
      clamp(bonus, -CAPTURE_HISTORY_BONUS_MIN, CAPTURE_HISTORY_BONUS_MAX);
  int clamped_malus =
      clamp(bonus, -CAPTURE_HISTORY_MALUS_MIN, CAPTURE_HISTORY_MALUS_MAX);
  int adjust = is_best_move ? clamped_bonus : -clamped_malus;
  thread->capture_history[thread->pos.mailbox[from]]
                         [thread->pos.mailbox[target]][from][target] +=
      adjust -
      thread->capture_history[thread->pos.mailbox[from]]
                             [thread->pos.mailbox[target]][from][target] *
          abs(adjust) / HISTORY_MAX;
}

static inline void update_continuation_history(thread_t *thread,
                                               searchstack_t *ss, int move,
                                               uint8_t depth,
                                               uint8_t is_best_move) {
  int prev_piece = ss->piece;
  int prev_target = get_move_target(ss->move);
  int piece = thread->pos.mailbox[get_move_source(move)];
  int target = get_move_target(move);
  int bonus = 16 * depth * depth + 32 * depth + 16;
  int clamped_bonus =
      clamp(bonus, -CONT_HISTORY_BONUS_MIN, CONT_HISTORY_BONUS_MAX);
  int clamped_malus =
      clamp(bonus, -CONT_HISTORY_MALUS_MIN, CONT_HISTORY_MALUS_MAX);
  int adjust = is_best_move ? clamped_bonus : -clamped_malus;
  thread->continuation_history[prev_piece][prev_target][piece][target] +=
      adjust -
      thread->continuation_history[prev_piece][prev_target][piece][target] *
          abs(adjust) / HISTORY_MAX;
}

static inline void update_pawn_history(thread_t *thread, int move,
                                       uint8_t depth, uint8_t is_best_move) {
  int target = get_move_target(move);
  int source = get_move_source(move);
  int bonus = 16 * depth * depth + 32 * depth + 16;
  int clamped_bonus =
      clamp(bonus, -QUIET_HISTORY_BONUS_MIN, QUIET_HISTORY_BONUS_MAX);
  int clamped_malus =
      clamp(bonus, -QUIET_HISTORY_MALUS_MIN, QUIET_HISTORY_MALUS_MAX);
  int adjust = is_best_move ? clamped_bonus : -clamped_malus;
  thread->pawn_history[thread->pos.hash_keys.pawn_key % 32767]
                      [thread->pos.mailbox[source]][target] +=
      adjust - thread->pawn_history[thread->pos.hash_keys.pawn_key % 32767]
                                   [thread->pos.mailbox[source]][target] *
                   abs(adjust) / HISTORY_MAX;
}

void update_quiet_history_moves(thread_t *thread, moves *quiet_moves,
                                int best_move, uint8_t depth) {
  for (uint32_t i = 0; i < quiet_moves->count; ++i) {
    if (quiet_moves->entry[i].move == best_move) {
      update_quiet_history(thread, best_move, depth, 1);
    } else {
      update_quiet_history(thread, quiet_moves->entry[i].move, depth, 0);
    }
  }
}

void update_capture_history_moves(thread_t *thread, moves *capture_moves,
                                  int best_move, uint8_t depth) {
  for (uint32_t i = 0; i < capture_moves->count; ++i) {
    if (capture_moves->entry[i].move == best_move) {
      update_capture_history(thread, best_move, depth, 1);
    } else {
      update_capture_history(thread, capture_moves->entry[i].move, depth, 0);
    }
  }
}

void update_continuation_history_moves(thread_t *thread, searchstack_t *ss,
                                       moves *quiet_moves, int best_move,
                                       uint8_t depth) {
  for (uint32_t i = 0; i < quiet_moves->count; ++i) {
    if (quiet_moves->entry[i].move == best_move) {
      update_continuation_history(thread, ss - 1, best_move, depth, 1);
      update_continuation_history(thread, ss - 2, best_move, depth, 1);
      update_continuation_history(thread, ss - 4, best_move, depth, 1);
    } else {
      update_continuation_history(thread, ss - 1, quiet_moves->entry[i].move,
                                  depth, 0);
      update_continuation_history(thread, ss - 2, quiet_moves->entry[i].move,
                                  depth, 0);
      update_continuation_history(thread, ss - 4, quiet_moves->entry[i].move,
                                  depth, 0);
    }
  }
}

void update_pawn_history_moves(thread_t *thread, moves *quiet_moves,
                               int best_move, uint8_t depth) {
  for (uint32_t i = 0; i < quiet_moves->count; ++i) {
    if (quiet_moves->entry[i].move == best_move) {
      update_pawn_history(thread, best_move, depth, 1);
    } else {
      update_pawn_history(thread, quiet_moves->entry[i].move, depth, 0);
    }
  }
}

int16_t get_conthist_score(thread_t *thread, searchstack_t *ss, int move) {
  return thread->continuation_history[ss->piece][get_move_target(
      ss->move)][thread->pos.mailbox[get_move_source(move)]]
                                     [get_move_target(move)];
}
