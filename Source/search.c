#include "search.h"
#include "attacks.h"
#include "bitboards.h"
#include "enums.h"
#include "evaluate.h"
#include "history.h"
#include "move.h"
#include "movegen.h"
#include "nnue.h"
#include "pyrrhic/tbprobe.h"
#include "see.h"
#include "stats.h"
#include "structs.h"
#include "syzygy.h"
#include "threads.h"
#include "transposition.h"
#include "uci.h"
#include "utils.h"
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern volatile uint8_t ABORT_SIGNAL;

extern int thread_count;

extern keys_t keys;

extern int QUIET_HISTORY_MAX_TT;
extern int QUIET_HISTORY_TT_FACTOR;
extern int QUIET_HISTORY_TT_BASE;

extern uint8_t disable_norm;
extern uint8_t minimal;

// Depths and untunable values (SPSA poison)
int RAZOR_DEPTH = 7;
int RFP_DEPTH = 9;
int FP_DEPTH = 10;
int NMP_BASE_REDUCTION = 6;
int NMP_DIVISER = 3;
int NMP_RED_MIN = 3;
int IIR_DEPTH = 4;
int SEE_DEPTH = 10;
int ASP_DEPTH = 4;
int SE_DEPTH = 6;
int PROBCUT_DEPTH = 5;
int PROBCUT_SHALLOW_DEPTH = 3;
int SE_DEPTH_REDUCTION = 6;
int IIR_DEPTH_REDUCTION = 3;
int EVAL_STABILITY_VAR = 9;
int QS_SEE_THRESHOLD = 7;
int SE_PV_DOUBLE_MARGIN = 1;
int SE_DOUBLE_MARGIN = 0;
int LMR_SHALLOWER_MARGIN = 6;

// SPSA Tuned params
int RAZOR_MARGIN = 291;
int RFP_MARGIN = 57;
int RFP_BASE_MARGIN = 26;
int RFP_IMPROVING = 60;
int RFP_OPP_WORSENING = 15;
int FP_MULTIPLIER = 133;
int FP_ADDITION = 168;
int FP_HISTORY_DIVISOR = 31;
int NMP_RED_DIVISER = 152;
int NMP_BASE_ADD = 197;
int NMP_MULTIPLIER = 21;
int SEE_QUIET = 42;
int SEE_CAPTURE = 29;
int SEE_HISTORY_DIVISOR = 37;
int SE_TRIPLE_MARGIN = 37;
int SE_BETA_BASE = 64;
int SE_BETA_MULTIPLIER = 66;
int LDSE_MARGIN = 25;
int LMR_PV_NODE = 839;
int LMR_HISTORY_QUIET = 1114;
int LMR_HISTORY_NOISY = 1073;
int LMR_WAS_IN_CHECK = 801;
int LMR_IN_CHECK = 990;
int LMR_CUTNODE = 1280;
int LMR_TT_DEPTH = 1086;
int LMR_TT_PV = 1133;
int LMR_TT_PV_CUTNODE = 822;
int LMR_TT_SCORE = 843;
int LMR_CUTOFF_CNT = 853;
int LMR_IMPROVING = 986;
int LMR_DEEPER_MARGIN = 33;
int LMP_BETA_MARGIN = 15;
int ASP_WINDOW = 14;
int QS_FUTILITY_THRESHOLD = 87;
int MO_SEE_THRESHOLD = 104;
int LMR_QUIET_HIST_DIV = 6576;
int LMR_CAPT_HIST_DIV = 8306;
int ASP_WINDOW_DIVISER = 28335;
int ASP_WINDOW_MULTIPLIER = 431;
int HINDSIGH_REDUCTION_ADD = 2903;
int HINDSIGH_REDUCTION_RED = 2022;
int HINDSIGN_REDUCTION_EVAL_MARGIN = 92;
int PROBCUT_MARGIN = 237;
int PROBCUT_SEE_THRESHOLD = 98;
int MO_SEE_HISTORY_DIVISER = 31;
int MO_QUIET_HIST_MULT = 1099;
int MO_CONT1_HIST_MULT = 919;
int MO_CONT2_HIST_MULT = 950;
int MO_CONT4_HIST_MULT = 1019;
int MO_PAWN_HIST_MULT = 948;
int MO_CAPT_HIST_MULT = 1123;
int MO_MVV_MULT = 15382;
int SEARCH_QUIET_HIST_MULT = 1024;
int SEARCH_CONT1_HIST_MULT = 1024;
int SEARCH_CONT2_HIST_MULT = 1024;
int SEARCH_PAWN_HIST_MULT = 1024;
int SEARCH_CAPT_HIST_MULT = 1024;
int SEARCH_MVV_MULT = 1024;

int QUIET_HISTORY_MALUS_MAX = 1019;
int QUIET_HISTORY_BONUS_MAX = 1122;
int QUIET_HISTORY_BASE_BONUS = 11;
int QUIET_HISTORY_FACTOR_BONUS = 155;
int QUIET_HISTORY_BASE_MALUS = 7;
int QUIET_HISTORY_FACTOR_MALUS = 219;
int QUIET_HISTORY_MAX_TT = 1395;
int QUIET_HISTORY_TT_FACTOR = 128;
int QUIET_HISTORY_TT_BASE = 73;

int CAPTURE_HISTORY_MALUS_MAX = 895;
int CAPTURE_HISTORY_BONUS_MAX = 1562;
int CAPTURE_HISTORY_BASE_BONUS = 11;
int CAPTURE_HISTORY_FACTOR_BONUS = 154;
int CAPTURE_HISTORY_BASE_MALUS = 10;
int CAPTURE_HISTORY_FACTOR_MALUS = 241;

int CONT_HISTORY_MALUS_MAX = 1253;
int CONT_HISTORY_BONUS_MAX = 2419;
int CONT_HISTORY_BASE_BONUS = 9;
int CONT_HISTORY_FACTOR_BONUS = 181;
int CONT_HISTORY_BASE_MALUS = 8;
int CONT_HISTORY_FACTOR_MALUS = 235;

int PAWN_HISTORY_MALUS_MAX = 945;
int PAWN_HISTORY_BONUS_MAX = 1350;
int PAWN_HISTORY_BASE_BONUS = 9;
int PAWN_HISTORY_FACTOR_BONUS = 191;
int PAWN_HISTORY_BASE_MALUS = 10;
int PAWN_HISTORY_FACTOR_MALUS = 138;

double LMP_MARGIN_WORSENING_BASE = 1.405891757145538;
double LMP_MARGIN_WORSENING_FACTOR = 0.4116244371474249;
double LMP_MARGIN_WORSENING_POWER = 1.5932724818048056;
double LMP_MARGIN_IMPROVING_BASE = 2.9151951127948252;
double LMP_MARGIN_IMPROVING_FACTOR = 0.9025404783844696;
double LMP_MARGIN_IMPROVING_POWER = 2.0358152631368465;

double LMR_OFFSET_QUIET = 0.7220082351499686;
double LMR_DIVISOR_QUIET = 1.7832888527819457;
double LMR_OFFSET_NOISY = -0.09296853281135277;
double LMR_DIVISOR_NOISY = 2.4534130204987474;

double NODE_TIME_MULTIPLIER = 2.3968333171483014;
double NODE_TIME_ADDITION = 0.46384927439622464;
double NODE_TIME_MIN = 0.5441111971890362;

double EVAL_TIME_ADDITION = 1.1898752474297039;
double EVAL_TIME_MULTIPLIER = 0.047348829007590354;

int SEEPieceValues[] = {61, 331, 327, 601, 1523, 0, 0};

int mvv[] = {131, 351, 315, 492, 1300, 0};

int lmr[2][MAX_PLY + 1][256];

double bestmove_scale[5] = {2.4136513491949803, 1.3631465643570557,
                            1.1066915352771052, 0.8951575452186968,
                            0.7113366999063576};

uint64_t nodes_spent_table[4096] = {0};

// Initializes the late move reduction array
void init_reductions(void) {
  for (int depth = 0; depth <= MAX_PLY; depth++) {
    for (int move = 0; move < 256; move++) {
      if (move == 0 || depth == 0) {
        lmr[0][depth][move] = 0;
        lmr[1][depth][move] = 0;
        continue;
      }
      lmr[0][depth][move] =
          LMR_OFFSET_NOISY + log(depth) * log(move) / LMR_DIVISOR_NOISY;
      lmr[1][depth][move] =
          LMR_OFFSET_QUIET + log(depth) * log(move) / LMR_DIVISOR_QUIET;
    }
  }
}

void scale_time(thread_t *thread, uint8_t best_move_stability,
                uint8_t eval_stability, uint16_t move) {
  double not_bm_nodes_fraction =
      1 - (double)nodes_spent_table[move >> 4] / (double)thread->nodes;
  double node_scaling_factor =
      MAX(NODE_TIME_MULTIPLIER * not_bm_nodes_fraction + NODE_TIME_ADDITION,
          NODE_TIME_MIN);
  double eval = EVAL_TIME_ADDITION - eval_stability * EVAL_TIME_MULTIPLIER;
  limits.soft_limit =
      MIN(thread->starttime + limits.base_soft *
                                  bestmove_scale[best_move_stability] * eval *
                                  node_scaling_factor,
          limits.max_time + thread->starttime);
}

uint8_t check_time(thread_t *thread) {
  // if time is up break here
  if (thread->index == 0 &&
      ((limits.timeset && ((thread->nodes % 1024) == 0) &&
        get_time_ms() > limits.hard_limit) ||
       (limits.nodes_set && thread->nodes >= limits.node_limit_hard))) {
    // tell engine to stop calculating
    thread->stopped = 1;
    return 1;
  }
  return 0;
}

// position repetition detection
static inline uint8_t is_repetition(thread_t *thread) {
  position_t *pos = &thread->positions[thread->ply];
  // loop over repetition indices range
  for (uint32_t index = 0; index < thread->repetition_index; index++)
    // if we found the hash key same with a current
    if (thread->repetition_table[index] == pos->hash_keys.hash_key)
      // we found a repetition
      return 1;

  // if no repetition found
  return 0;
}

static inline uint8_t only_pawns(position_t *pos) {
  return !((pos->bitboards[N] | pos->bitboards[n] | pos->bitboards[B] |
            pos->bitboards[b] | pos->bitboards[R] | pos->bitboards[r] |
            pos->bitboards[Q] | pos->bitboards[q]) &
           pos->occupancies[pos->side]);
}

static inline uint32_t pack(move_t m, uint16_t i) {
  const int score = m.score;
  return (uint32_t)(score + (1 << 23)) << 8 | (i ^ 0xff);
}

static inline uint16_t unpack(uint32_t packed) {
  return (packed & 0xff) ^ 0xff;
}

static inline move_t pick_next_best_move(moves *move_list, uint16_t *index) {
  if (*index >= move_list->count)
    return (move_t){0}; // Return dummy if we're out of bounds

  const int initial_index = *index;
  const int len = move_list->count;

  move_t *const moves = move_list->entry;

  uint32_t best_packed = pack(moves[initial_index], initial_index);

  for (int i = initial_index + 1; i < len; ++i) {
    best_packed = MAX(best_packed, pack(moves[i], i));
  }

  uint16_t best = unpack(best_packed);

  // Swap best with current index
  if (best != initial_index) {
    move_t temp = moves[initial_index];
    moves[initial_index] = moves[best];
    moves[best] = temp;
  }

  // Return and increment index for next call
  return move_list->entry[(*index)++];
}

// Scores noisy moves and splits them into good/bad lists based on SEE
static inline void score_noisy(thread_t *thread, searchstack_t *ss,
                               moves *noisy_list, moves *good_noisy,
                               moves *bad_noisy, uint16_t tt_move) {
  position_t *pos = &thread->positions[thread->ply];
  for (uint32_t i = 0; i < noisy_list->count; i++) {
    move_t entry = noisy_list->entry[i];
    uint16_t move = entry.move;

    if (move == tt_move)
      continue;

    uint8_t source = get_move_source(move);
    uint8_t target = get_move_target(move);
    uint8_t source_threatened = is_square_threatened(ss, source);
    uint8_t target_threatened = is_square_threatened(ss, target);

    int target_piece;
    if (get_move_enpassant(move))
      target_piece = pos->mailbox[pos->side ? target - 8 : target + 8];
    else
      target_piece = pos->mailbox[target];

    entry.score = mvv[target_piece % 6] * MO_MVV_MULT;
    entry.score +=
        thread->capture_history[pos->mailbox[source]][target_piece][source]
                               [target][source_threatened][target_threatened] *
        MO_CAPT_HIST_MULT;
    entry.score /= 1024;

    int see_threshold =
        -MO_SEE_THRESHOLD - entry.score / MO_SEE_HISTORY_DIVISER;
    if (SEE(pos, move, see_threshold))
      good_noisy->entry[good_noisy->count++] = entry;
    else
      bad_noisy->entry[bad_noisy->count++] = entry;
  }
}

// Scores quiet moves in place
static inline void score_quiet(thread_t *thread, searchstack_t *ss,
                               moves *quiet_list, uint16_t tt_move) {
  position_t *pos = &thread->positions[thread->ply];
  for (uint32_t i = 0; i < quiet_list->count; i++) {
    move_t *entry = &quiet_list->entry[i];
    uint16_t move = entry->move;

    if (move == tt_move) {
      entry->score = -(1 << 20);
      continue;
    }

    uint8_t source = get_move_source(move);
    uint8_t target = get_move_target(move);
    uint8_t source_threatened = is_square_threatened(ss, source);
    uint8_t target_threatened = is_square_threatened(ss, target);

    entry->score =
        thread->quiet_history[pos->side][source][target][source_threatened]
                             [target_threatened] *
            MO_QUIET_HIST_MULT +
        get_conthist_score(thread, ss, move, 1) * MO_CONT1_HIST_MULT +
        get_conthist_score(thread, ss, move, 2) * MO_CONT2_HIST_MULT +
        get_conthist_score(thread, ss, move, 4) * MO_CONT4_HIST_MULT +
        thread->pawn_history[pos->hash_keys.pawn_key % 2048]
                            [pos->mailbox[source]][target] *
            MO_PAWN_HIST_MULT;
    entry->score /= 1024;
  }
}

typedef enum {
  STAGE_TABLE = 0,
  STAGE_GENERATE_NOISY,
  STAGE_GOOD_NOISY,
  STAGE_GENERATE_QUIET,
  STAGE_QUIET,
  STAGE_BAD_NOISY,
  STAGE_DONE,
} picker_stage_t;

typedef struct {
  picker_stage_t stage;
  moves good_noisy;
  moves bad_noisy;
  moves quiets;
  uint16_t good_noisy_index;
  uint16_t bad_noisy_index;
  uint16_t quiet_index;
  uint16_t tt_move;
  uint8_t generate_all;
  uint8_t skip_quiets;
  thread_t *thread;
  searchstack_t *ss;
} picker_t;

static inline void init_picker(picker_t *picker, thread_t *thread,
                               searchstack_t *ss, uint16_t tt_move,
                               uint8_t generate_all) {
  picker->stage = STAGE_TABLE;
  picker->good_noisy.count = 0;
  picker->bad_noisy.count = 0;
  picker->quiets.count = 0;
  picker->good_noisy_index = 0;
  picker->bad_noisy_index = 0;
  picker->quiet_index = 0;
  picker->tt_move = tt_move;
  picker->generate_all = generate_all;
  picker->skip_quiets = 0;
  picker->thread = thread;
  picker->ss = ss;
}

static inline uint16_t select_next(picker_t *picker) {
  position_t *pos = &picker->thread->positions[picker->thread->ply];

  switch (picker->stage) {

  case STAGE_TABLE:
    picker->stage = STAGE_GENERATE_NOISY;
    if (picker->tt_move != 0 &&
        (picker->generate_all || get_move_capture(picker->tt_move) ||
         is_move_promotion(picker->tt_move)) &&
        is_pseudo_legal(pos, picker->tt_move) && is_legal(pos, picker->tt_move))
      return picker->tt_move;
    /* fallthrough */

  case STAGE_GENERATE_NOISY: {
    moves tmp;
    generate_noisy(pos, &tmp, 0);
    score_noisy(picker->thread, picker->ss, &tmp, &picker->good_noisy,
                &picker->bad_noisy, picker->tt_move);
    picker->stage = STAGE_GOOD_NOISY;
    /* fallthrough */
  }

  case STAGE_GOOD_NOISY:
    while (picker->good_noisy_index < picker->good_noisy.count)
      return pick_next_best_move(&picker->good_noisy, &picker->good_noisy_index)
          .move;
    if (!picker->generate_all) {
      picker->stage = STAGE_DONE;
      return 0;
    }
    picker->stage = STAGE_GENERATE_QUIET;
    /* fallthrough */

  case STAGE_GENERATE_QUIET:
    if (picker->skip_quiets) {
      picker->stage = STAGE_BAD_NOISY;
    } else {
      generate_quiets(pos, &picker->quiets, 0);
      score_quiet(picker->thread, picker->ss, &picker->quiets, picker->tt_move);
      picker->stage = STAGE_QUIET;
    }
    /* fallthrough */

  case STAGE_QUIET:
    if (picker->skip_quiets) {
      picker->stage = STAGE_BAD_NOISY;
    } else {
      while (picker->quiet_index < picker->quiets.count) {
        uint16_t move =
            pick_next_best_move(&picker->quiets, &picker->quiet_index).move;
        if (move != picker->tt_move)
          return move;
      }
      picker->stage = STAGE_BAD_NOISY;
    }
    /* fallthrough */

  case STAGE_BAD_NOISY:
    while (picker->bad_noisy_index < picker->bad_noisy.count)
      return pick_next_best_move(&picker->bad_noisy, &picker->bad_noisy_index)
          .move;
    picker->stage = STAGE_DONE;
    /* fallthrough */

  case STAGE_DONE:
    return 0;

  default:
    return 0;
  }
}

// quiescence search
static inline int16_t quiescence(thread_t *thread, searchstack_t *ss,
                                 int16_t alpha, int16_t beta, uint8_t pv_node) {
  const uint8_t ply = thread->ply;
  // Derive current position from the thread's position stack.
  position_t *pos = &thread->positions[ply];

  // Check on time
  if (check_time(thread)) {
    stop_threads(thread, thread_count);
    return 0;
  }

  // we are too deep, hence there's an overflow of arrays relying on max ply
  // constant
  if (ply > MAX_PLY - 4) {
    // evaluate position
    return evaluate(thread, pos, &thread->accumulator[ply]);
  }

  if (ply > thread->seldepth) {
    thread->seldepth = ply;
  }

  uint16_t best_move = 0;
  uint16_t tt_move = 0;
  int16_t score = NO_SCORE, best_score = NO_SCORE, futility_score = NO_SCORE;
  int16_t raw_static_eval = NO_SCORE;
  int16_t tt_score = NO_SCORE;
  int16_t tt_static_eval = NO_SCORE;
  uint8_t tt_hit = 0;
  uint8_t tt_flag = HASH_FLAG_EXACT;
  uint8_t tt_was_pv = pv_node;

  tt_entry_t *tt_entry = read_hash_entry(pos, &tt_hit);

  if (tt_hit) {
    tt_move = tt_entry->move;
    tt_was_pv |= tt_entry->tt_pv;
    tt_score = score_from_tt(ply, tt_entry->score);
    tt_static_eval = tt_entry->static_eval;
    tt_flag = tt_entry->flag;
  }

  // If we arent in PV node and we hit requirements for cutoff
  // we can return early from search
  if (!pv_node && can_use_score(alpha, beta, tt_score, tt_flag)) {
    return tt_score;
  }

  const uint8_t in_check = stm_in_check(pos);

  if (in_check) {
    ss->static_eval = NO_SCORE;
    raw_static_eval = NO_SCORE;
  } else {
    if (tt_hit && tt_static_eval != NO_SCORE) {
      raw_static_eval = tt_static_eval;
      ss->static_eval = best_score =
          adjust_static_eval(thread, ss, raw_static_eval);

      if (tt_score != NO_SCORE && ((tt_flag == HASH_FLAG_EXACT) ||
                                   ((tt_flag == HASH_FLAG_UPPER_BOUND) &&
                                    (tt_score < ss->static_eval)) ||
                                   ((tt_flag == HASH_FLAG_LOWER_BOUND) &&
                                    (tt_score > ss->static_eval)))) {
        best_score = tt_score;
      }
    } else {
      raw_static_eval = evaluate(thread, pos, &thread->accumulator[ply]);
      ss->static_eval = best_score =
          adjust_static_eval(thread, ss, raw_static_eval);
    }

    // fail-hard beta cutoff
    if (best_score >= beta) {
      if (!tt_hit) {
        write_hash_entry(tt_entry, pos, ply, NO_SCORE, raw_static_eval, 0, 0,
                         HASH_FLAG_NONE, tt_was_pv);
      }
      if (!is_decisive(best_score) && !is_decisive(beta)) {
        best_score = (best_score + beta) / 2;
      }
      // node (position) fails high
      return best_score;
    }

    // found a better move
    alpha = MAX(alpha, best_score);

    futility_score = best_score + QS_FUTILITY_THRESHOLD;
  }

  picker_t picker;
  init_picker(&picker, thread, ss, tt_move, in_check);

  moves capture_list[1];
  capture_list->count = 0;

  uint16_t previous_square = 0;
  uint16_t moves_seen = 0;

  if ((ss - 1)->move != 0) {
    previous_square = get_move_target((ss - 1)->move);
  }

  // loop over moves within a movelist
  uint16_t move;
  while ((move = select_next(&picker)) != 0) {

    if (!is_legal(pos, move)) {
      continue;
    }

    moves_seen++;

    if (!is_loss(best_score)) {
      if (!SEE(pos, move, -QS_SEE_THRESHOLD))
        continue;

      if (get_move_target(move) != previous_square) {
        if (moves_seen >= 3 && !is_direct_check(pos, move)) {
          continue;
        }
      }

      if (!in_check && get_move_capture(move) && futility_score <= alpha &&
          !SEE(pos, move, 1)) {
        best_score = MAX(best_score, futility_score);
        continue;
      }
    }

    // Copy current ply's position to the next ply slot and advance.
    thread->positions[++thread->ply] = *pos;
    position_t *next_pos = &thread->positions[thread->ply];

    // increment repetition index & store hash key
    thread->repetition_index++;
    thread->repetition_table[thread->repetition_index] =
        next_pos->hash_keys.hash_key;

    // make move on the new ply's position
    make_move(next_pos, move);

    calculate_threats(next_pos, ss + 1);

    update_nnue(next_pos, thread, pos->mailbox, move);

    ss->move = move;
    ss->piece = pos->mailbox[get_move_source(move)];

    thread->nodes++;

    if (get_move_capture(move)) {
      add_move(capture_list, move);
    }

    prefetch_hash_entry(next_pos->hash_keys.hash_key);

    // score current move
    score = -quiescence(thread, ss + 1, -beta, -alpha, pv_node);

    // restore ply (position is unchanged at thread->ply, no board restore
    // needed)
    thread->ply--;
    thread->repetition_index--;

    // return 0 if time is up
    if (thread->stopped == 1) {
      return 0;
    }

    if (score > best_score) {
      best_score = score;
      // found a better move
      if (score > alpha) {
        alpha = score;
        best_move = move;
        // fail-hard beta cutoff
        if (alpha >= beta) {
          int capt_bonus = 165;
          int capt_malus = -251;
          for (uint32_t i = 0; i < capture_list->count; ++i) {
            if (capture_list->entry[i].move == best_move) {
              update_capture_history(thread, ss, best_move, capt_bonus);
            } else {
              update_capture_history(thread, ss, capture_list->entry[i].move,
                                     capt_malus);
            }
          }
          break;
        }
      }
    }
  }

  // we don't have any legal moves to make in the current postion
  if (moves_seen == 0) {
    // king is in check
    if (in_check)
      // return mating score (assuming closest distance to mating position)
      return -MATE_VALUE + ply;
  }

  uint8_t hash_flag = HASH_FLAG_NONE;
  if (alpha >= beta) {
    hash_flag = HASH_FLAG_LOWER_BOUND;
  } else {
    hash_flag = HASH_FLAG_UPPER_BOUND;
  }

  write_hash_entry(tt_entry, pos, ply, best_score, raw_static_eval, 0,
                   best_move, hash_flag, tt_was_pv);

  return best_score;
}

static inline void update_pv(PV_t *pv, uint8_t ply, uint16_t move) {
  const uint8_t child_len = pv->pv_length[ply + 1];
  pv->pv_table[ply][0] = move;
  memcpy(&pv->pv_table[ply][1], pv->pv_table[ply + 1],
         child_len * sizeof(uint16_t));
  pv->pv_length[ply] = child_len + 1;
}

// negamax alpha beta search
static inline int16_t negamax(thread_t *thread, searchstack_t *ss,
                              int16_t alpha, int16_t beta, int depth,
                              uint8_t cutnode, uint8_t pv_node) {
  const uint8_t ply = thread->ply;
  // Derive current position from the thread's position stack.
  position_t *pos = &thread->positions[ply];

  // we are too deep, hence there's an overflow of arrays relying on max ply
  // constant
  if (ply > MAX_PLY - 4) {
    // evaluate position
    return evaluate(thread, pos, &thread->accumulator[ply]);
  }

  // Reset PV length for this ply so stale continuations aren't inherited
  // if no alpha improvement occurs here.
  thread->pv.pv_length[ply] = 0;

  // variable to store current move's score (from the static evaluation
  // perspective)
  int16_t raw_static_eval = NO_SCORE;

  uint16_t tt_move = 0;
  int16_t tt_score = NO_SCORE;
  int16_t tt_static_eval = NO_SCORE;
  uint8_t tt_hit = 0;
  uint8_t tt_depth = 0;
  uint8_t tt_flag = HASH_FLAG_EXACT;
  ss->tt_pv = ss->excluded_move ? ss->tt_pv : pv_node;

  uint8_t root_node = ply == 0;
  uint8_t all_node = !(pv_node || cutnode);

  // Limit depth to MAX_PLY - 1 in case extensions make it too big
  depth = clamp(depth, 0, MAX_PLY - 1);

  if (depth == 0 && ply > thread->seldepth) {
    thread->seldepth = ply;
  }

  if (!root_node) {
    // if position repetition occurs
    if (is_repetition(thread) || pos->fifty >= 100) {
      // return draw score
      return 1 - (thread->nodes & 2);
    }

    // Mate distance pruning
    alpha = MAX(alpha, -MATE_VALUE + (int)ply);
    beta = MIN(beta, MATE_VALUE - (int)ply - 1);
    if (alpha >= beta)
      return alpha;
  }

  // is king in check
  uint8_t in_check = stm_in_check(pos);

  // recursion escape condition
  if (depth <= 0) {
    // run quiescence search
    return quiescence(thread, ss, alpha, beta, pv_node);
  }

  tt_entry_t *tt_entry = read_hash_entry(pos, &tt_hit);

  if (tt_hit) {
    ss->tt_pv |= tt_entry->tt_pv;
    tt_score = score_from_tt(ply, tt_entry->score);
    tt_static_eval = tt_entry->static_eval;
    tt_depth = tt_entry->depth;
    tt_flag = tt_entry->flag;
    tt_move = tt_entry->move;
  }

  // If we arent in excluded move or PV node and we hit requirements for cutoff
  // we can return early from search
  if (!ss->excluded_move && !pv_node && tt_depth >= depth &&
      can_use_score(alpha, beta, tt_score, tt_flag)) {
    if (tt_move != 0 &&
        !(is_move_promotion(tt_move) || get_move_capture(tt_move)) &&
        tt_score >= beta) {
      int16_t bonus =
          MIN(QUIET_HISTORY_MAX_TT,
              (QUIET_HISTORY_TT_FACTOR * depth - QUIET_HISTORY_TT_BASE));
      update_quiet_history(thread, ss, tt_move, bonus);
    }
    return tt_score;
  }

  if (in_check) {
    ss->static_eval = NO_SCORE;
    raw_static_eval = NO_SCORE;
    ss->eval = NO_SCORE;
  } else if (ss->excluded_move) {
    raw_static_eval = ss->eval = ss->static_eval;
  } else if (tt_hit) {
    raw_static_eval = tt_static_eval != NO_SCORE
                          ? tt_static_eval
                          : evaluate(thread, pos, &thread->accumulator[ply]);
    ss->eval = ss->static_eval =
        adjust_static_eval(thread, ss, raw_static_eval);

    if (tt_score != NO_SCORE &&
        ((tt_flag == HASH_FLAG_UPPER_BOUND && tt_score < ss->eval) ||
         (tt_flag == HASH_FLAG_LOWER_BOUND && tt_score > ss->eval) ||
         (tt_flag == HASH_FLAG_EXACT))) {
      ss->eval = tt_score;
    }
  } else {
    raw_static_eval = evaluate(thread, pos, &thread->accumulator[ply]);
    ss->eval = ss->static_eval =
        adjust_static_eval(thread, ss, raw_static_eval);

    write_hash_entry(tt_entry, pos, ply, NO_SCORE, raw_static_eval, 0, 0,
                     HASH_FLAG_NONE, ss->tt_pv);
  }

  int16_t correction = correction_value(thread);
  (void)correction;

  uint8_t initial_depth = depth;
  int32_t improvement = 0;
  uint8_t opponent_worsening = 0;

  if ((ss - 2)->static_eval != NO_SCORE && !in_check) {
    improvement = ss->static_eval > (ss - 2)->static_eval;
  }
  if (!in_check) {
    opponent_worsening = ss->static_eval + (ss - 1)->static_eval > 1;
  }

  uint8_t improving = improvement > 0;

  (ss + 2)->cutoff_cnt = 0;

  // Check on time
  if (check_time(thread)) {
    stop_threads(thread, thread_count);
    return 0;
  }

  if (!root_node && !in_check && !ss->excluded_move) {
    if ((ss - 1)->reduction >= HINDSIGH_REDUCTION_ADD && !opponent_worsening) {
      ++depth;
    }

    if (depth >= 2 && (ss - 1)->reduction >= HINDSIGH_REDUCTION_RED &&
        (ss - 1)->eval != NO_SCORE &&
        ss->static_eval + (ss - 1)->eval > HINDSIGN_REDUCTION_EVAL_MARGIN) {
      --depth;
    }
  }

  // moves seen counter
  uint16_t moves_seen = 0;

  // Razoring
  if (!pv_node && !in_check && !ss->excluded_move && depth <= RAZOR_DEPTH &&
      ss->static_eval + RAZOR_MARGIN * depth < alpha) {
    const int16_t razor_score = quiescence(thread, ss, alpha, beta, NON_PV);
    if (razor_score <= alpha) {
      return razor_score;
    }
  }

  // Reverse Futility Pruning
  if (!ss->tt_pv && !ss->excluded_move && depth <= RFP_DEPTH &&
      !is_loss(beta) && !is_win(ss->eval) &&
      ss->eval >= beta + RFP_BASE_MARGIN + RFP_MARGIN * depth -
                      RFP_IMPROVING * improving -
                      RFP_OPP_WORSENING * opponent_worsening) {
    // evaluation margin substracted from static evaluation score
    return beta + (ss->eval - beta) / 3;
  }

  // Null Move Pruning
  if (cutnode && !in_check && !ss->excluded_move && !ss->null_move &&
      ply > thread->nmp_min_ply && ss->eval >= beta &&
      ss->static_eval >= beta - NMP_MULTIPLIER * depth + NMP_BASE_ADD &&
      ss->eval >= ss->static_eval && !is_loss(beta) && !only_pawns(pos)) {
    int R = depth / NMP_DIVISER + NMP_BASE_REDUCTION;
    R = MIN(R, depth);

    // Copy current position to the next ply slot and advance.
    null_move_copy_accumulator(thread, ply, ply + 1);
    thread->positions[++thread->ply] = *pos;
    position_t *null_pos = &thread->positions[thread->ply];

    // increment repetition index & store hash key
    thread->repetition_index++;
    thread->repetition_table[thread->repetition_index] =
        null_pos->hash_keys.hash_key;

    // hash enpassant if available
    if (null_pos->enpassant != no_sq)
      null_pos->hash_keys.hash_key ^= keys.enpassant_keys[null_pos->enpassant];

    // reset enpassant capture square
    null_pos->enpassant = no_sq;

    // update pins on the pre-null-move position (same pieces, just need pin
    // info for the null move evaluation)
    update_slider_pins(pos, white);
    update_slider_pins(pos, black);

    // switch the side, literally giving opponent an extra move to make
    null_pos->side ^= 1;

    // hash the side
    null_pos->hash_keys.hash_key ^= keys.side_key;

    prefetch_hash_entry(null_pos->hash_keys.hash_key);

    ss->move = 0;
    ss->piece = NO_PIECE;
    null_pos->checkers = 0;
    null_pos->checker_count = 0;
    (ss + 1)->null_move = 1;

    calculate_threats(null_pos, ss + 1);

    /* search moves with reduced depth to find beta cutoffs
       depth - 1 - R where R is a reduction limit */
    int16_t score =
        -negamax(thread, ss + 1, -beta, -beta + 1, depth - R, !cutnode, NON_PV);

    (ss + 1)->null_move = 0;

    // restore ply (original position at thread->ply is unchanged)
    thread->ply--;
    thread->repetition_index--;

    // return 0 if time is up
    if (thread->stopped == 1) {
      return 0;
    }

    // fail-hard beta cutoff
    if (score >= beta && !is_win(score)) {
      if (thread->nmp_min_ply != 0 || depth <= 14) {
        return score;
      }
      thread->nmp_min_ply = ply + 3 * (depth - R) / 4;

      const int16_t verification_score =
          negamax(thread, ss, beta - 1, beta, depth - R, 0, 0);

      thread->nmp_min_ply = 0;

      if (verification_score >= beta) {
        return verification_score;
      }
    }
  }

  const int16_t probcut_beta = beta + PROBCUT_MARGIN;

  // ProbCut pruning
  if (!pv_node && !in_check && !ss->excluded_move && depth >= PROBCUT_DEPTH &&
      !is_win(beta) &&
      (!tt_hit || tt_depth + 3 < depth ||
       (tt_score >= probcut_beta && !is_decisive(tt_score)))) {
    int probcut_depth = depth - PROBCUT_SHALLOW_DEPTH - 1;
    probcut_depth = MAX(1, probcut_depth);

    // Generate captures and good promotions for ProbCut
    picker_t probcut_picker;
    init_picker(&probcut_picker, thread, ss, 0, 0);

    // Try moves that look promising
    uint16_t move;
    while ((move = select_next(&probcut_picker)) != 0) {

      // Skip moves that don't pass SEE threshold
      if (!SEE(pos, move, probcut_beta - ss->static_eval)) {
        continue;
      }

      if (!is_legal(pos, move)) {
        continue;
      }

      // Copy current position to the next ply slot and advance.
      thread->positions[++thread->ply] = *pos;
      position_t *next_pos = &thread->positions[thread->ply];

      // increment repetition index & store hash key
      thread->repetition_index++;
      thread->repetition_table[thread->repetition_index] =
          next_pos->hash_keys.hash_key;

      // make move on the new ply's position
      make_move(next_pos, move);

      calculate_threats(next_pos, ss + 1);
      update_nnue(next_pos, thread, pos->mailbox, move);

      ss->move = move;
      ss->piece = pos->mailbox[get_move_source(move)];

      thread->nodes++;

      prefetch_hash_entry(next_pos->hash_keys.hash_key);

      // Shallow search with raised beta
      int16_t probcut_score =
          -quiescence(thread, ss + 1, -probcut_beta, -probcut_beta + 1, NON_PV);

      // If qsearch doesn't fail high, try a deeper search
      if (probcut_score >= probcut_beta) {
        probcut_score =
            -negamax(thread, ss + 1, -probcut_beta, -probcut_beta + 1,
                     probcut_depth, !cutnode, NON_PV);
      }

      // Restore ply (original position at thread->ply is unchanged)
      thread->ply--;
      thread->repetition_index--;

      // Check if we need to stop
      if (thread->stopped == 1) {
        return 0;
      }

      // If shallow search failed high, we can prune
      if (probcut_score >= probcut_beta) {
        // Store in transposition table
        write_hash_entry(tt_entry, pos, ply, probcut_score, raw_static_eval,
                         probcut_depth + 1, move, HASH_FLAG_LOWER_BOUND,
                         ss->tt_pv);
        return probcut_score;
      }
    }
  }

  // Internal Iterative Reductions
  if (!all_node && !ss->excluded_move && depth >= IIR_DEPTH &&
      (!tt_move || tt_depth < depth - IIR_DEPTH_REDUCTION ||
       tt_flag == HASH_FLAG_UPPER_BOUND)) {
    depth--;
  }

  int extensions = 0;

  // Singular Extensions
  // A rather simple idea that if our TT move is accurate we run a reduced
  // search to see if we can beat this score. If not we extend the TT move
  // search
  if (ply < thread->depth * 2 && !root_node && depth >= SE_DEPTH &&
      !ss->excluded_move && tt_depth >= depth - SE_DEPTH_REDUCTION &&
      tt_flag != HASH_FLAG_UPPER_BOUND && abs(tt_score) < MATE_SCORE) {
    const int s_beta = tt_score - (SE_BETA_BASE + SE_BETA_MULTIPLIER *
                                                      (ss->tt_pv && !pv_node)) *
                                      depth / 55;
    const int s_depth = depth / 2;

    ss->excluded_move = tt_move;

    // Singular search at the same ply (thread->ply is unchanged)
    const int16_t s_score =
        negamax(thread, ss, s_beta - 1, s_beta, s_depth, cutnode, NON_PV);

    ss->excluded_move = 0;

    // No move beat tt score so we extend the search
    if (s_score < s_beta) {
      const int16_t double_margin =
          SE_DOUBLE_MARGIN + SE_PV_DOUBLE_MARGIN * pv_node;
      const int16_t triple_margin = SE_TRIPLE_MARGIN;
      extensions++;
      extensions += s_score < s_beta - double_margin;
      if (!get_move_capture(tt_move)) {
        extensions += s_score < s_beta - triple_margin;
      }
    }

    // Multicut: Singular search failed high so if singular beta beats our
    // beta we can assume the main search will also fail high and thus we can
    // just cutoff here
    else if (s_beta >= beta) {
      return s_beta;
    }

    // Negative Extensions
    else if (tt_score >= beta) {
      extensions -= 2 + !pv_node;
    }

    else if (cutnode) {
      extensions -= 2;
    }
  }
  // Low Depth Singular Extensions (LDSE)
  else if (depth <= 7 && !in_check && ss->static_eval <= alpha - LDSE_MARGIN &&
           tt_flag == HASH_FLAG_LOWER_BOUND) {
    extensions = 1;
  }

  picker_t picker;
  init_picker(&picker, thread, ss, tt_move, 1);

  moves quiet_list[1];
  moves capture_list[1];
  quiet_list->count = 0;
  capture_list->count = 0;

  int16_t best_score = NO_SCORE;

  uint16_t best_move = 0;

  uint8_t bound = HASH_FLAG_UPPER_BOUND;

  // loop over moves within a movelist
  uint16_t move;
  while ((move = select_next(&picker)) != 0) {
    uint8_t quiet =
        (get_move_capture(move) == 0 && is_move_promotion(move) == 0);

    if (move == ss->excluded_move) {
      continue;
    }

    if (!is_legal(pos, move)) {
      continue;
    }

    moves_seen++;

    ss->history_score =
        quiet
            ? thread->quiet_history[pos->side][get_move_source(move)]
                                   [get_move_target(move)][is_square_threatened(
                                       ss, get_move_source(move))]
                                   [is_square_threatened(
                                       ss, get_move_target(move))] *
                      SEARCH_QUIET_HIST_MULT +
                  get_conthist_score(thread, ss, move, 1) *
                      SEARCH_CONT1_HIST_MULT +
                  get_conthist_score(thread, ss, move, 2) *
                      SEARCH_CONT2_HIST_MULT
            : thread->capture_history
                          [pos->mailbox[get_move_source(move)]]
                          [pos->mailbox[get_move_target(move)]]
                          [get_move_source(move)][get_move_target(move)]
                          [is_square_threatened(ss, get_move_source(move))]
                          [is_square_threatened(ss, get_move_target(move))] *
                      SEARCH_CAPT_HIST_MULT +
                  mvv[pos->mailbox[get_move_target(move)] % 6] *
                      SEARCH_MVV_MULT;
    ss->history_score /= 1024;

    if (!root_node && !is_loss(best_score)) {
      int lmp_treshold;

      if (improving || ss->static_eval >= beta + LMP_BETA_MARGIN) {
        lmp_treshold = LMP_MARGIN_IMPROVING_BASE +
                       LMP_MARGIN_IMPROVING_FACTOR *
                           pow(initial_depth, LMP_MARGIN_IMPROVING_POWER);
      } else {
        lmp_treshold = LMP_MARGIN_WORSENING_BASE +
                       LMP_MARGIN_WORSENING_FACTOR *
                           pow(initial_depth, LMP_MARGIN_WORSENING_POWER);
      }

      // Late Move Pruning
      if (!pv_node && quiet && moves_seen >= lmp_treshold && !only_pawns(pos)) {
        picker.skip_quiets = 1;
      }

      int r = lmr[quiet][MIN(63, depth)][MIN(63, moves_seen)];
      r += !pv_node;
      int lmr_depth = MAX(1, depth - 1 - MAX(r, 1));
      // Futility Pruning
      if (lmr_depth <= FP_DEPTH && !in_check && quiet &&
          ss->static_eval + lmr_depth * FP_MULTIPLIER + FP_ADDITION +
                  ss->history_score / FP_HISTORY_DIVISOR <=
              alpha &&
          !is_direct_check(pos, move)) {
        picker.skip_quiets = 1;
        continue;
      }

      if (quiet && depth <= 10 && ss->history_score < -2300 * depth * depth) {
        picker.skip_quiets = 1;
        continue;
      }

      int noisy_futility_margin = ss->static_eval + 150 * depth;
      if (!in_check && depth < 10 && picker.stage == STAGE_BAD_NOISY &&
          noisy_futility_margin <= alpha && !is_direct_check(pos, move)) {
        break;
      }

      int see_treshold;
      if (!get_move_capture(move)) {
        see_treshold = -SEE_QUIET * depth;
      } else {
        see_treshold = -SEE_CAPTURE * depth * depth;
      }

      // SEE PVS Pruning
      if (depth <= SEE_DEPTH &&
          !SEE(pos, move,
               see_treshold - ss->history_score / SEE_HISTORY_DIVISOR))
        continue;
    }

    // Copy current position to the next ply slot and advance.
    thread->positions[++thread->ply] = *pos;
    position_t *next_pos = &thread->positions[thread->ply];

    // increment repetition index & store hash key
    thread->repetition_index++;
    thread->repetition_table[thread->repetition_index] =
        next_pos->hash_keys.hash_key;

    // make move on the new ply's position
    make_move(next_pos, move);

    calculate_threats(next_pos, ss + 1);

    update_nnue(next_pos, thread, pos->mailbox, move);

    ss->move = move;
    ss->piece = pos->mailbox[get_move_source(move)];

    // increment nodes count
    thread->nodes++;

    if (quiet) {
      add_move(quiet_list, move);
    } else {
      add_move(capture_list, move);
    }

    prefetch_hash_entry(next_pos->hash_keys.hash_key);

    uint64_t nodes_before_search = thread->nodes;

    // PVS & LMR
    int new_depth = moves_seen == 1 ? depth + extensions - 1 : depth - 1;

    int16_t score = NO_SCORE;

    // LMR
    if (depth >= 2 && moves_seen > 1 + root_node) {
      int R = lmr[quiet][depth][MIN(255, moves_seen)] * 1024;
      R += !pv_node * LMR_PV_NODE;
      R -= ss->history_score * (quiet ? LMR_HISTORY_QUIET : LMR_HISTORY_NOISY) /
           (quiet ? LMR_QUIET_HIST_DIV : LMR_CAPT_HIST_DIV);
      R -= in_check * LMR_WAS_IN_CHECK;
      R += cutnode * LMR_CUTNODE;
      R -= (tt_depth >= depth) * LMR_TT_DEPTH;
      R -= ss->tt_pv * LMR_TT_PV;
      R += (ss->tt_pv && tt_hit && tt_score <= alpha) * LMR_TT_SCORE;
      R -= (ss->tt_pv && cutnode) * LMR_TT_PV_CUTNODE;
      R -= stm_in_check(next_pos) * LMR_IN_CHECK; // check on the new position
      R += (ss->cutoff_cnt > 3) * LMR_CUTOFF_CNT;
      R -= improving * LMR_IMPROVING;
      R += (bound == HASH_FLAG_EXACT) * 1024;
      ;

      ss->reduction = R;

      R = R / 1024;
      int reduced_depth =
          MAX(1, MIN(new_depth - R, new_depth + cutnode)) + pv_node;

      score = -negamax(thread, ss + 1, -alpha - 1, -alpha, reduced_depth, 1,
                       NON_PV);
      ss->reduction = 0;

      if (score > alpha && R != 0) {
        new_depth += (score > best_score + LMR_DEEPER_MARGIN);
        new_depth -= (score < best_score + LMR_SHALLOWER_MARGIN);

        if (new_depth > reduced_depth) {
          score = -negamax(thread, ss + 1, -alpha - 1, -alpha, new_depth,
                           !cutnode, NON_PV);
        }
      }
      // Full Depth Search
    } else if (!pv_node || moves_seen > 1) {
      score = -negamax(thread, ss + 1, -alpha - 1, -alpha, new_depth, !cutnode,
                       NON_PV);
    }

    // Principal Variation Search
    if (pv_node && (moves_seen == 1 || score > alpha)) {
      score = -negamax(thread, ss + 1, -beta, -alpha, new_depth, 0, PV_NODE);
    }

    // restore ply (original position at thread->ply is unchanged)
    thread->ply--;
    thread->repetition_index--;

    if (thread->index == 0 && root_node) {
      nodes_spent_table[move >> 4] += thread->nodes - nodes_before_search;
    }

    // return 0 if time is up
    if (thread->stopped == 1) {
      return 0;
    }

    // found a better move
    if (score > best_score) {
      best_score = score;
      if (score > alpha) {
        best_move = move;
        bound = HASH_FLAG_EXACT;

        // PV node (position)
        alpha = score;

        if (pv_node)
          update_pv(&thread->pv, ply, move);

        // fail-hard beta cutoff
        if (alpha >= beta) {
          bound = HASH_FLAG_LOWER_BOUND;
          // on quiet moves
          if (!(get_move_capture(best_move) || is_move_promotion(best_move))) {
            int history_depth = depth;
            history_depth += (!in_check && ss->eval <= alpha);
            int cont_bonus = MIN(CONT_HISTORY_BASE_BONUS +
                                     CONT_HISTORY_FACTOR_BONUS * history_depth,
                                 CONT_HISTORY_BONUS_MAX);
            int cont_malus = -MIN(CONT_HISTORY_BASE_MALUS +
                                      CONT_HISTORY_FACTOR_MALUS * history_depth,
                                  CONT_HISTORY_MALUS_MAX);

            int quiet_bonus =
                MIN(QUIET_HISTORY_BASE_BONUS +
                        QUIET_HISTORY_FACTOR_BONUS * history_depth,
                    QUIET_HISTORY_BONUS_MAX);
            int quiet_malus =
                -MIN(QUIET_HISTORY_BASE_MALUS +
                         QUIET_HISTORY_FACTOR_MALUS * history_depth,
                     QUIET_HISTORY_MALUS_MAX);

            int pawn_bonus = MIN(PAWN_HISTORY_BASE_BONUS +
                                     PAWN_HISTORY_FACTOR_BONUS * history_depth,
                                 PAWN_HISTORY_BONUS_MAX);
            int pawn_malus = -MIN(PAWN_HISTORY_BASE_MALUS +
                                      PAWN_HISTORY_FACTOR_MALUS * history_depth,
                                  PAWN_HISTORY_MALUS_MAX);
            for (uint32_t i = 0; i < quiet_list->count; ++i) {
              uint16_t move = quiet_list->entry[i].move;
              if (move == best_move) {
                update_continuation_histories(thread, ss, best_move,
                                              cont_bonus);
                update_pawn_history(thread, best_move, pawn_bonus);
                update_quiet_history(thread, ss, best_move, quiet_bonus);
              } else {
                update_continuation_histories(thread, ss, move, cont_malus);
                update_pawn_history(thread, move, pawn_malus);
                update_quiet_history(thread, ss, move, quiet_malus);
              }
            }
          }

          int capt_bonus = MIN(CAPTURE_HISTORY_BASE_BONUS +
                                   CAPTURE_HISTORY_FACTOR_BONUS * depth,
                               CAPTURE_HISTORY_BONUS_MAX);
          int capt_malus = -MIN(CAPTURE_HISTORY_BASE_MALUS +
                                    CAPTURE_HISTORY_FACTOR_MALUS * depth,
                                CAPTURE_HISTORY_MALUS_MAX);
          for (uint32_t i = 0; i < capture_list->count; ++i) {
            if (capture_list->entry[i].move == best_move) {
              update_capture_history(thread, ss, best_move, capt_bonus);
            } else {
              update_capture_history(thread, ss, capture_list->entry[i].move,
                                     capt_malus);
            }
          }
          ss->cutoff_cnt++;
          break;
        }
      }
    }
  }

  // we don't have any legal moves to make in the current postion
  if (moves_seen == 0) {
    // king is in check
    if (in_check)
      // return mating score (assuming closest distance to mating position)
      return -MATE_VALUE + ply;

    // king is not in check
    else
      // return stalemate score
      return 0;
  }

  if (!root_node && best_score >= beta && !is_decisive(best_score) &&
      !is_decisive(alpha)) {
    best_score = (best_score * depth + beta) / (depth + 1);
  }

  if (!ss->excluded_move) {
    // store hash entry with the score equal to alpha
    write_hash_entry(tt_entry, pos, ply, best_score, raw_static_eval, depth,
                     best_move, bound, ss->tt_pv);

    if (!in_check &&
        !(get_move_capture(best_move) || is_move_promotion(best_move)) &&
        (bound != HASH_FLAG_LOWER_BOUND || best_score > raw_static_eval) &&
        (bound != HASH_FLAG_UPPER_BOUND || best_score <= raw_static_eval)) {
      update_corrhist(thread, ss, raw_static_eval, best_score, depth);
    }
  }

  // node (position) fails low
  return best_score;
}

static void print_thinking(thread_t *thread, int16_t score,
                           uint8_t current_depth) {

  uint64_t nodes = total_nodes(thread, thread_count);
  uint64_t time = get_time_ms() - thread->starttime;
  uint64_t nps = (nodes / fmax(time, 1)) * 1000;

  printf("info depth %d seldepth %d score ", current_depth, thread->seldepth);

  if (score > -MATE_VALUE && score < -MATE_SCORE) {
    printf("mate %d ", -(score + MATE_VALUE) / 2 - 1);
  } else if (score > MATE_SCORE && score < MATE_VALUE) {
    printf("mate %d ", (MATE_VALUE - score) / 2 + 1);
  } else {
    if (disable_norm) {
      printf("cp %d ", score);
    } else {
      printf("cp %d ", 100 * score / 215);
    }
  }
  printf("nodes %" PRIu64 " ", nodes);
  printf("nps %" PRIu64 " ", nps);
  printf("hashfull %d ", hash_full());
  printf("time %" PRIu64 " ", time);
  printf("pv ");

  // loop over the moves within a PV line
  for (int count = 0; count < thread->pv.pv_length[0]; count++) {
    // print PV move
    print_move(thread->pv.pv_table[0][count]);
    printf(" ");
  }

  // print new line
  printf("\n");
}

void *iterative_deepening(void *thread_void) {
  thread_t *thread = (thread_t *)thread_void;
  position_t *pos = &thread->positions[0];

  uint16_t prev_best_move = 0;
  int16_t average_score = NO_SCORE;
  uint8_t best_move_stability = 0;
  uint8_t eval_stability = 0;

  // iterative deepening
  for (thread->depth = 1; thread->depth <= limits.depth; thread->depth++) {
    // if time is up
    if (thread->stopped == 1) {
      // stop calculating and return best move so far
      break;
    }

    // define initial alpha beta bounds
    int16_t alpha = -INF;
    int16_t beta = INF;

    searchstack_t ss[MAX_PLY + 10];
    for (int i = 0; i < MAX_PLY + 10; ++i) {
      ss[i].excluded_move = 0;
      ss[i].static_eval = NO_SCORE;
      ss[i].eval = NO_SCORE;
      ss[i].history_score = 0;
      ss[i].move = 0;
      ss[i].piece = NO_PIECE;
      ss[i].null_move = 0;
      ss[i].reduction = 0;
      ss[i].tt_pv = 0;
      ss[i].cutoff_cnt = 0;
    }

    calculate_threats(pos, ss + 7);

    thread->seldepth = 0;

    uint16_t window = ASP_WINDOW;

    uint8_t fail_high_count = 0;

    while (true) {

      if (check_time(thread)) {
        stop_threads(thread, thread_count);
        break;
      }

      if (thread->stopped) {
        break;
      }

      if (thread->depth >= ASP_DEPTH) {
        window += thread->score * thread->score / ASP_WINDOW_DIVISER;

        alpha = MAX(-INF, thread->score - window);
        beta = MIN(INF, thread->score + window);
      }

      // find best move within a given position
      // negamax reads root position from thread->positions[0] via
      // thread->ply==0
      thread->score = negamax(thread, ss + 7, alpha, beta,
                              thread->depth - fail_high_count, 0, PV_NODE);

      // We hit an aspiration window cut-off before time ran out and we jumped
      // to another depth with wider search which we didnt finish
      if (thread->stopped) {
        return NULL;
      }

      if (thread->score <= alpha) {
        beta = (alpha + beta) / 2;

        alpha = MAX(-INF, alpha - window);
        fail_high_count = 0;
        window += 28 * window / 128;
      }

      else if (thread->score >= beta) {
        beta = MIN(INF, beta + window);

        window += 62 * window / 128;

        if (alpha < 2000) {
          ++fail_high_count;
        }
      } else {
        average_score = average_score == NO_SCORE
                            ? thread->score
                            : (average_score + thread->score) / 2;
        break;
      }
    }

    if (thread->index == 0) {
      if (thread->pv.pv_table[0][0] == prev_best_move) {
        best_move_stability = MIN(best_move_stability + 1, 4);
      } else {
        prev_best_move = thread->pv.pv_table[0][0];
        best_move_stability = 0;
      }

      if (thread->score > average_score - EVAL_STABILITY_VAR &&
          thread->score < average_score + EVAL_STABILITY_VAR) {
        eval_stability = MIN(eval_stability + 1, 8);
      } else {
        eval_stability = 0;
      }

      if (limits.timeset && thread->depth > 7) {
        scale_time(thread, best_move_stability, eval_stability,
                   thread->pv.pv_table[0][0]);
      }
    }

    if (thread->index == 0 &&
        ((limits.timeset && get_time_ms() >= limits.soft_limit) ||
         (limits.nodes_set && thread->nodes >= limits.node_limit_soft))) {
      stop_threads(thread, thread_count);
    }

    if (thread->index == 0 && !minimal) {
      // if PV is available
      if (thread->pv.pv_length[0]) {
        // print search info
        print_thinking(thread, thread->score, thread->depth);
      }
    }

    if (thread->stopped) {
      return NULL;
    }
  }
  return NULL;
}

// search position for the best move
// TODO: Pass in const ply so we can always restore it to
// original without search changing it
void search_position(position_t *pos, thread_t *threads) {
  pthread_t pthreads[thread_count];
  for (int i = 0; i < thread_count; ++i) {
    threads[i].nodes = 0;
    threads[i].stopped = 0;
    threads[i].positions[threads[0].ply] = *pos;
    threads[i].ply = threads[0].ply;
    threads[i].score = -INF;
    threads[i].quit = 0;
    threads[i].nmp_min_ply = 0;
    memset(&threads[i].pv, 0, sizeof(threads[i].pv));
    memset(&threads[i].neurons, 0, sizeof(simd_t));
    init_accumulator(pos, threads[i].accumulator);
    init_finny_tables(&threads[i], pos);
    if (i > 0) {
      threads[i].repetition_index = threads[0].repetition_index;
      memcpy(threads[i].repetition_table, threads[0].repetition_table,
             sizeof(threads[0].repetition_table));
    }
  }

  // clear helper data structures for search
  memset(nodes_spent_table, 0, sizeof(nodes_spent_table));

  for (int thread_index = 1; thread_index < thread_count; ++thread_index) {
    pthread_create(&pthreads[thread_index], NULL, &iterative_deepening,
                   &threads[thread_index]);
  }

  iterative_deepening(&threads[0]);

  stop_threads(threads, thread_count);

  for (int i = 1; i < thread_count; ++i) {
    pthread_join(pthreads[i], NULL);
  }

  // print best move
  printf("bestmove ");
  if (threads->pv.pv_table[0][0]) {
    print_move(threads->pv.pv_table[0][0]);
  } else {
    printf("(none)");
  }
  printf("\n");
}
