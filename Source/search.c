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
#include "structs.h"
#include "syzygy.h"
#include "threads.h"
#include "transposition.h"
#include "uci.h"
#include "utils.h"
#include <inttypes.h>
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
int SE_PV_DOUBLE_MARGIN = 1;
int SE_TRIPLE_MARGIN = 36;
int IIR_DEPTH_REDUCTION = 3;
int EVAL_STABILITY_VAR = 9;

// SPSA Tuned params
int RAZOR_MARGIN = 265;
int RFP_MARGIN = 56;
int RFP_BASE_MARGIN = 25;
int RFP_IMPROVING = 59;
int RFP_OPP_WORSENING = 13;
int FP_MULTIPLIER = 126;
int FP_ADDITION = 148;
int FP_HISTORY_DIVISOR = 31;
int NMP_RED_DIVISER = 168;
int NMP_BASE_ADD = 177;
int NMP_MULTIPLIER = 21;
int SEE_QUIET = 46;
int SEE_CAPTURE = 29;
int SEE_HISTORY_DIVISOR = 43;
int LMR_PV_NODE = 888;
int LMR_HISTORY_QUIET = 1153;
int LMR_HISTORY_NOISY = 1026;
int LMR_WAS_IN_CHECK = 847;
int LMR_IN_CHECK = 1032;
int LMR_CUTNODE = 1094;
int LMR_TT_DEPTH = 1214;
int LMR_TT_PV = 1100;
int LMR_TT_PV_CUTNODE = 776;
int LMR_TT_SCORE = 946;
int LMR_CUTOFF_CNT = 1024;
int LMR_IMPROVING = 1024;
int LMR_DEEPER_MARGIN = 34;
int LMR_SHALLOWER_MARGIN = 6;
int ASP_WINDOW = 14;
int QS_SEE_THRESHOLD = 7;
int QS_FUTILITY_THRESHOLD = 100;
int MO_SEE_THRESHOLD = 123;
int LMR_QUIET_HIST_DIV = 6408;
int LMR_CAPT_HIST_DIV = 7292;
int ASP_WINDOW_DIVISER = 31350;
int ASP_WINDOW_MULTIPLIER = 475;
int HINDSIGH_REDUCTION_ADD = 3072;
int HINDSIGH_REDUCTION_RED = 2048;
int HINDSIGN_REDUCTION_EVAL_MARGIN = 96;
int PROBCUT_MARGIN = 200;
int PROBCUT_SEE_THRESHOLD = 100;
int MO_QUIET_HIST_MULT = 1024;
int MO_CONT1_HIST_MULT = 1024;
int MO_CONT2_HIST_MULT = 1024;
int MO_CONT4_HIST_MULT = 1024;
int MO_PAWN_HIST_MULT = 1024;
int MO_CAPT_HIST_MULT = 1024;
int MO_MVV_MULT = 1024;
int SEARCH_QUIET_HIST_MULT = 1024;
int SEARCH_CONT1_HIST_MULT = 1024;
int SEARCH_CONT2_HIST_MULT = 1024;
int SEARCH_PAWN_HIST_MULT = 1024;
int SEARCH_CAPT_HIST_MULT = 1024;
int SEARCH_MVV_MULT = 1024;
double LMR_DEEPER_MULT = 1.8637336462306593f;

double LMP_MARGIN_WORSENING_BASE = 1.4130549930204508;
double LMP_MARGIN_WORSENING_FACTOR = 0.42425725461783326;
double LMP_MARGIN_WORSENING_POWER = 1.66320815674854;
double LMP_MARGIN_IMPROVING_BASE = 2.800936684508665;
double LMP_MARGIN_IMPROVING_FACTOR = 0.8477227043572519;
double LMP_MARGIN_IMPROVING_POWER = 2.053175027274589;

double LMR_OFFSET_QUIET = 0.7673533691303112;
double LMR_DIVISOR_QUIET = 1.8182016135452135;
double LMR_OFFSET_NOISY = -0.13955338058414543;
double LMR_DIVISOR_NOISY = 2.5646204692563113;

double NODE_TIME_MULTIPLIER = 2.386305469085633;
double NODE_TIME_ADDITION = 0.44814808632195424;
double NODE_TIME_MIN = 0.5568573339779463;

double EVAL_TIME_ADDITION = 1.2;
double EVAL_TIME_MULTIPLIER = 0.04;

int SEEPieceValues[] = {56, 311, 324, 582, 1225, 0, 0};

int mvv[] = {126, 395, 309, 584, 1347, 0};

int lmr[2][MAX_PLY + 1][256];

int SEE_MARGIN[MAX_PLY + 1][2];

int LMP_MARGIN[MAX_PLY + 1][2];

double bestmove_scale[5] = {2.435008962486456f, 1.3514595123975768f,
                            1.0921709375887645f, 0.8799608961420715f,
                            0.7006821873450457f};

uint64_t nodes_spent_table[4096] = {0};

// Initializes the late move reduction array
void init_reductions(void) {
  for (int depth = 0; depth <= MAX_PLY; depth++) {
    SEE_MARGIN[depth][0] = -SEE_CAPTURE * depth * depth;
    SEE_MARGIN[depth][1] = -SEE_QUIET * depth;
    LMP_MARGIN[depth][0] =
        LMP_MARGIN_WORSENING_BASE +
        LMP_MARGIN_WORSENING_FACTOR *
            pow(depth, LMP_MARGIN_WORSENING_POWER); // non-improving
    LMP_MARGIN[depth][1] =
        LMP_MARGIN_IMPROVING_BASE +
        LMP_MARGIN_IMPROVING_FACTOR *
            pow(depth, LMP_MARGIN_IMPROVING_POWER); // improving
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
      ((limits.timeset && get_time_ms() > limits.hard_limit) ||
       (limits.nodes_set && thread->nodes >= limits.node_limit_hard))) {
    // tell engine to stop calculating
    thread->stopped = 1;
    return 1;
  }
  return 0;
}

// score moves
static inline void score_move(position_t *pos, thread_t *thread,
                              searchstack_t *ss, move_t *move_entry,
                              uint16_t hash_move) {
  uint16_t move = move_entry->move;
  uint8_t promoted_piece = get_move_promoted(pos->side, move);

  if (move == hash_move) {
    move_entry->score = 2000000000;
    return;
  }

  if (promoted_piece) {
    // We have a promotion
    switch (promoted_piece) {
    case q:
    case Q:
      move_entry->score = 1410000000;
      break;
    case n:
    case N:
      move_entry->score = 1400000000;
      break;
    default:
      move_entry->score = -800000000;
      break;
    }
    if (get_move_capture(move)) {
      // The promotion is a capture and we check SEE score
      if (SEE(pos, move, -MO_SEE_THRESHOLD)) {
        return;
      } else {
        // Capture failed SEE and thus gets ordered at the bottom of the list
        move_entry->score = -700000000;
        return;
      }
    } else {
      // We have a promotion that is not a capture. Order it below good capture
      // promotions.
      move_entry->score -= 100000;
      return;
    }
  }

  move_entry->score = 0;

  // score capture move
  if (get_move_capture(move)) {
    // init target piece
    int target_piece = get_move_enpassant(move) == 0
                           ? pos->mailbox[get_move_target(move)]
                       : pos->side ? pos->mailbox[get_move_target(move) - 8]
                                   : pos->mailbox[get_move_target(move) + 8];

    // score move by MVV LVA lookup [source piece][target piece]
    move_entry->score += mvv[target_piece % 6] * MO_MVV_MULT;
    move_entry->score +=
        thread
            ->capture_history[pos->mailbox[get_move_source(move)]][target_piece]
                             [get_move_source(move)][get_move_target(move)] *
        MO_CAPT_HIST_MULT;
    move_entry->score /= 1024;
    move_entry->score +=
        SEE(pos, move, -MO_SEE_THRESHOLD) ? 1000000000 : -1000000000;
    return;
  }

  // score quiet move
  else {
    // score history move
    move_entry->score =
        thread->quiet_history[pos->side][get_move_source(move)][get_move_target(
            move)][is_square_threatened(ss, get_move_source(move))]
                             [is_square_threatened(ss, get_move_target(move))] *
            MO_QUIET_HIST_MULT +
        get_conthist_score(thread, pos, ss, move, 1) * MO_CONT1_HIST_MULT +
        get_conthist_score(thread, pos, ss, move, 2) * MO_CONT2_HIST_MULT +
        get_conthist_score(thread, pos, ss, move, 4) * MO_CONT4_HIST_MULT +
        thread->pawn_history[pos->hash_keys.pawn_key % 2048]
                            [pos->mailbox[get_move_source(move)]]
                            [get_move_target(move)] *
            MO_PAWN_HIST_MULT;
    move_entry->score /= 1024;
    return;
  }
  move_entry->score = 0;
  return;
}

static inline move_t pick_next_best_move(moves *move_list, uint16_t *index) {
  if (*index >= move_list->count)
    return (move_t){0}; // Return dummy if we're out of bounds

  uint16_t best = *index;

  for (uint16_t i = *index + 1; i < move_list->count; ++i) {
    if (move_list->entry[i].score > move_list->entry[best].score)
      best = i;
  }

  // Swap best with current index
  if (best != *index) {
    move_t temp = move_list->entry[*index];
    move_list->entry[*index] = move_list->entry[best];
    move_list->entry[best] = temp;
  }

  // Return and increment index for next call
  return move_list->entry[(*index)++];
}

// position repetition detection
static inline uint8_t is_repetition(position_t *pos, thread_t *thread) {
  // loop over repetition indices range
  for (uint32_t index = 0; index < thread->repetition_index; index++)
    // if we found the hash key same with a current
    if (thread->repetition_table[index] == pos->hash_keys.hash_key)
      // we found a repetition
      return 1;

  // if no repetition found
  return 0;
}

static inline uint8_t is_material_draw(position_t *pos) {
  uint8_t piece_count = __builtin_popcountll(pos->occupancies[both]);

  // K v K
  if (piece_count == 2) {
    return 1;
  }
  // Initialize knight and bishop count only after we check that piece count is
  // higher then 2 as there cannot be a knight or bishop with 2 pieces on the
  // board
  uint8_t knight_count =
      __builtin_popcountll(pos->bitboards[n] | pos->bitboards[N]);
  // KN v K || KB v K
  if (piece_count == 3 &&
      (knight_count == 1 ||
       __builtin_popcountll(pos->bitboards[b] | pos->bitboards[B]) == 1)) {
    return 1;
  } else if (piece_count == 4) {
    // KNN v K || KN v KN
    if (knight_count == 2) {
      return 1;
    }
    // KB v KB
    else if (__builtin_popcountll(pos->bitboards[b]) == 1 &&
             __builtin_popcountll(pos->bitboards[B]) == 1) {
      return 1;
    }
  }
  return 0;
}

static inline uint8_t only_pawns(position_t *pos) {
  return !((pos->bitboards[N] | pos->bitboards[n] | pos->bitboards[B] |
            pos->bitboards[b] | pos->bitboards[R] | pos->bitboards[r] |
            pos->bitboards[Q] | pos->bitboards[q]) &
           pos->occupancies[pos->side]);
}

// quiescence search
static inline int16_t quiescence(position_t *pos, thread_t *thread,
                                 searchstack_t *ss, int16_t alpha, int16_t beta,
                                 uint8_t pv_node) {
  // Check on time
  if (check_time(thread)) {
    stop_threads(thread, thread_count);
    return 0;
  }

  // we are too deep, hence there's an overflow of arrays relying on max ply
  // constant
  if (pos->ply > MAX_PLY - 4) {
    // evaluate position
    return evaluate(thread, pos, &thread->accumulator[pos->ply]);
  }

  if (pos->ply > thread->seldepth) {
    thread->seldepth = pos->ply;
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
    tt_score = score_from_tt(pos, tt_entry->score);
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
          adjust_static_eval(thread, pos, raw_static_eval);

      if (tt_score != NO_SCORE && ((tt_flag == HASH_FLAG_EXACT) ||
                                   ((tt_flag == HASH_FLAG_UPPER_BOUND) &&
                                    (tt_score < ss->static_eval)) ||
                                   ((tt_flag == HASH_FLAG_LOWER_BOUND) &&
                                    (tt_score > ss->static_eval)))) {
        best_score = tt_score;
      }
    } else {
      raw_static_eval = evaluate(thread, pos, &thread->accumulator[pos->ply]);
      ss->static_eval = best_score =
          adjust_static_eval(thread, pos, raw_static_eval);
    }

    // fail-hard beta cutoff
    if (best_score >= beta) {
      if (!tt_hit) {
        write_hash_entry(tt_entry, pos, NO_SCORE, raw_static_eval, 0, 0,
                         HASH_FLAG_NONE, tt_was_pv);
      }
      if (abs(best_score) < MATE_SCORE && abs(beta) < MATE_SCORE) {
        best_score = (best_score + beta) / 2;
      }
      // node (position) fails high
      return best_score;
    }

    // found a better move
    alpha = MAX(alpha, best_score);

    futility_score = best_score + QS_FUTILITY_THRESHOLD;
  }

  // create move list instance
  moves move_list[1];
  moves capture_list[1];
  capture_list->count = 0;

  // generate moves
  if (!in_check) {
    generate_noisy(pos, move_list);
  } else {
    generate_moves(pos, move_list);
  }

  for (uint32_t count = 0; count < move_list->count; count++) {
    score_move(pos, thread, ss, &move_list->entry[count], tt_move);
  }

  uint16_t move_index = 0;

  uint16_t previous_square = 0;

  uint16_t moves_seen = 0;

  if ((ss - 1)->move != 0) {
    previous_square = get_move_target((ss - 1)->move);
  }

  // loop over moves within a movelist

  while (move_index < move_list->count) {
    uint16_t move = pick_next_best_move(move_list, &move_index).move;

    if (!SEE(pos, move, -QS_SEE_THRESHOLD))
      continue;

    if (best_score > -MATE_SCORE && get_move_target(move) != previous_square) {
      if (move_index >= 3) {
        continue;
      }
    }

    if (!in_check && get_move_capture(move) && futility_score <= alpha &&
        !SEE(pos, move, 1)) {
      best_score = MAX(best_score, futility_score);
      continue;
    }

    // preserve board state
    position_t pos_copy = *pos;

    // increment ply
    pos_copy.ply++;

    // increment repetition index & store hash key
    thread->repetition_index++;
    thread->repetition_table[thread->repetition_index] =
        pos_copy.hash_keys.hash_key;

    // make sure to make only legal moves
    if (make_move(&pos_copy, move) == 0) {
      // decrement ply
      pos_copy.ply--;

      // decrement repetition index
      thread->repetition_index--;

      // skip to next move
      continue;
    }

    calculate_threats(pos, ss + 1);

    update_nnue(&pos_copy, thread, pos->mailbox, move);

    ss->move = move;
    ss->piece = pos->mailbox[get_move_source(move)];

    thread->nodes++;

    moves_seen++;

    if (get_move_capture(move)) {
      add_move(capture_list, move);
    }

    prefetch_hash_entry(pos_copy.hash_keys.hash_key);

    // score current move
    score = -quiescence(&pos_copy, thread, ss + 1, -beta, -alpha, pv_node);

    // decrement ply
    pos_copy.ply--;

    // decrement repetition index
    thread->repetition_index--;

    // take move back
    //*pos = pos_copy;

    // return 0 if time is up
    if (thread->stopped == 1) {
      return 0;
    }

    if (score > best_score) {
      best_score = score;
      best_move = move;
      // found a better move
      if (score > alpha) {
        alpha = score;
        // fail-hard beta cutoff
        if (alpha >= beta) {
          update_capture_history_moves(thread, pos, capture_list, best_move, 1);
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
      return -MATE_VALUE + pos->ply;
  }

  uint8_t hash_flag = HASH_FLAG_NONE;
  if (alpha >= beta) {
    hash_flag = HASH_FLAG_LOWER_BOUND;
  } else {
    hash_flag = HASH_FLAG_UPPER_BOUND;
  }

  write_hash_entry(tt_entry, pos, best_score, raw_static_eval, 0, best_move,
                   hash_flag, tt_was_pv);

  return best_score;
}

// negamax alpha beta search
static inline int16_t negamax(position_t *pos, thread_t *thread,
                              searchstack_t *ss, int16_t alpha, int16_t beta,
                              int depth, uint8_t cutnode, uint8_t pv_node) {
  // we are too deep, hence there's an overflow of arrays relying on max ply
  // constant
  if (pos->ply > MAX_PLY - 4) {
    // evaluate position
    return evaluate(thread, pos, &thread->accumulator[pos->ply]);
  }

  // init PV length
  thread->pv.pv_length[pos->ply] = pos->ply;

  // variable to store current move's score (from the static evaluation
  // perspective)
  int16_t current_score = NO_SCORE;
  int16_t raw_static_eval = NO_SCORE;

  uint16_t tt_move = 0;
  int16_t tt_score = NO_SCORE;
  int16_t tt_static_eval = NO_SCORE;
  uint8_t tt_hit = 0;
  uint8_t tt_depth = 0;
  uint8_t tt_flag = HASH_FLAG_EXACT;
  ss->tt_pv = ss->excluded_move ? ss->tt_pv : pv_node;

  uint8_t root_node = pos->ply == 0;

  // Limit depth to MAX_PLY - 1 in case extensions make it too big
  depth = clamp(depth, 0, MAX_PLY - 1);

  if (depth == 0 && pos->ply > thread->seldepth) {
    thread->seldepth = pos->ply;
  }

  if (!root_node) {
    // if position repetition occurs
    if (is_repetition(pos, thread) || pos->fifty >= 100 ||
        is_material_draw(pos)) {
      // return draw score
      return 1 - (thread->nodes & 2);
    }

    // Mate distance pruning
    alpha = MAX(alpha, -MATE_VALUE + (int)pos->ply);
    beta = MIN(beta, MATE_VALUE - (int)pos->ply - 1);
    if (alpha >= beta)
      return alpha;
  }

  // is king in check
  uint8_t in_check = stm_in_check(pos);

  // recursion escape condition
  if (depth <= 0) {
    // run quiescence search
    return quiescence(pos, thread, ss, alpha, beta, pv_node);
  }

  tt_entry_t *tt_entry = read_hash_entry(pos, &tt_hit);

  if (tt_hit) {
    ss->tt_pv |= tt_entry->tt_pv;
    tt_score = score_from_tt(pos, tt_entry->score);
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
      update_quiet_history(thread, pos, ss, tt_move, bonus);
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
    raw_static_eval =
        tt_static_eval != NO_SCORE
            ? tt_static_eval
            : evaluate(thread, pos, &thread->accumulator[pos->ply]);
    ss->eval = ss->static_eval =
        adjust_static_eval(thread, pos, raw_static_eval);

    if (tt_score != NO_SCORE &&
        ((tt_flag == HASH_FLAG_UPPER_BOUND && tt_score < ss->eval) ||
         (tt_flag == HASH_FLAG_LOWER_BOUND && tt_score > ss->eval) ||
         (tt_flag == HASH_FLAG_EXACT))) {
      ss->eval = tt_score;
    }
  } else {
    raw_static_eval = evaluate(thread, pos, &thread->accumulator[pos->ply]);
    ss->eval = ss->static_eval =
        adjust_static_eval(thread, pos, raw_static_eval);

    write_hash_entry(tt_entry, pos, NO_SCORE, raw_static_eval, 0, 0,
                     HASH_FLAG_NONE, ss->tt_pv);
  }

  int16_t correction = correction_value(thread, pos);
  (void)correction;

  uint8_t initial_depth = depth;
  uint8_t improving = 0;
  uint8_t opponent_worsening = 0;

  if ((ss - 2)->static_eval != NO_SCORE) {
    improving = ss->static_eval > (ss - 2)->static_eval;
  }
  if (!in_check) {
    opponent_worsening = ss->static_eval + (ss - 1)->static_eval > 1;
  }

  (ss + 2)->cutoff_cnt = 0;

  // Check on time
  if (check_time(thread)) {
    stop_threads(thread, thread_count);
    return 0;
  }

  if ((ss - 1)->reduction >= HINDSIGH_REDUCTION_ADD && !opponent_worsening) {
    ++depth;
  }

  if (depth >= 2 && (ss - 1)->reduction >= HINDSIGH_REDUCTION_RED &&
      (ss - 1)->eval != NO_SCORE &&
      ss->static_eval + (ss - 1)->eval > HINDSIGN_REDUCTION_EVAL_MARGIN) {
    --depth;
  }

  // moves seen counter
  uint16_t moves_seen = 0;

  // Razoring
  if (!pv_node && !in_check && !ss->excluded_move && depth <= RAZOR_DEPTH &&
      ss->static_eval + RAZOR_MARGIN * depth < alpha) {
    const int16_t razor_score =
        quiescence(pos, thread, ss, alpha, beta, NON_PV);
    if (razor_score <= alpha) {
      return razor_score;
    }
  }

  // Reverse Futility Pruning
  if (!ss->tt_pv && !in_check && !ss->excluded_move && depth <= RFP_DEPTH &&
      beta > -MATE_SCORE && ss->eval < MATE_SCORE &&
      ss->eval >= beta + RFP_BASE_MARGIN + RFP_MARGIN * depth -
                      RFP_IMPROVING * improving -
                      RFP_OPP_WORSENING * opponent_worsening + correction / 1024) {
    // evaluation margin substracted from static evaluation score
    return beta + (ss->eval - beta) / 3;
  }

  // Null Move Pruning
  if (!pv_node && !in_check && !ss->excluded_move && !ss->null_move &&
      pos->ply > thread->nmp_min_ply && ss->eval >= beta &&
      ss->static_eval >= beta - NMP_MULTIPLIER * depth + NMP_BASE_ADD &&
      ss->eval >= ss->static_eval && !only_pawns(pos)) {
    int R = depth / NMP_DIVISER + NMP_BASE_REDUCTION;
    R = MIN(R, depth);
    // preserve board state
    position_t pos_copy = *pos;

    thread->accumulator[pos_copy.ply + 1] = thread->accumulator[pos_copy.ply];

    // increment ply
    pos_copy.ply++;

    // increment repetition index & store hash key
    thread->repetition_index++;
    thread->repetition_table[thread->repetition_index] =
        pos_copy.hash_keys.hash_key;

    // hash enpassant if available
    if (pos_copy.enpassant != no_sq)
      pos_copy.hash_keys.hash_key ^= keys.enpassant_keys[pos_copy.enpassant];

    // reset enpassant capture square
    pos_copy.enpassant = no_sq;

    // switch the side, literally giving opponent an extra move to make
    pos_copy.side ^= 1;

    // hash the side
    pos_copy.hash_keys.hash_key ^= keys.side_key;

    prefetch_hash_entry(pos_copy.hash_keys.hash_key);

    ss->move = 0;
    ss->piece = NO_PIECE;
    pos_copy.checkers = 0;
    pos_copy.checker_count = 0;
    (ss + 1)->null_move = 1;

    calculate_threats(pos, ss + 1);

    /* search moves with reduced depth to find beta cutoffs
       depth - 1 - R where R is a reduction limit */
    current_score = -negamax(&pos_copy, thread, ss + 1, -beta, -beta + 1,
                             depth - R, !cutnode, NON_PV);

    (ss + 1)->null_move = 0;

    // decrement ply
    pos_copy.ply--;

    // decrement repetition index
    thread->repetition_index--;

    // restore board state
    //*pos = pos_copy;

    // return 0 if time is up
    if (thread->stopped == 1) {
      return 0;
    }

    // fail-hard beta cutoff
    if (current_score >= beta) {
      if (thread->nmp_min_ply != 0 || depth <= 14) {
        return current_score >= MATE_SCORE ? beta : current_score;
      }
      thread->nmp_min_ply = pos->ply + 3 * (depth - R) / 4;

      const int16_t verification_score =
          negamax(pos, thread, ss, beta - 1, beta, depth - R, 0, 0);

      thread->nmp_min_ply = 0;

      if (verification_score >= beta) {
        return verification_score;
      }
    }
  }

  // ProbCut pruning
  if (!pv_node && !in_check && !ss->excluded_move && depth >= PROBCUT_DEPTH &&
      abs(beta) < MATE_SCORE &&
      (!tt_hit || tt_depth + 3 < depth || tt_score >= beta + PROBCUT_MARGIN)) {

    int16_t probcut_beta = beta + PROBCUT_MARGIN;
    int probcut_depth = depth - PROBCUT_SHALLOW_DEPTH - 1;

    // Generate captures and good promotions for ProbCut
    moves probcut_list[1];
    generate_noisy(pos, probcut_list);

    // Score the moves
    for (uint32_t count = 0; count < probcut_list->count; count++) {
      score_move(pos, thread, ss, &probcut_list->entry[count], 0);
    }

    uint16_t probcut_index = 0;

    // Try moves that look promising
    while (probcut_index < probcut_list->count) {
      uint16_t move = pick_next_best_move(probcut_list, &probcut_index).move;

      // Skip moves that don't pass SEE threshold
      if (!SEE(pos, move, PROBCUT_SEE_THRESHOLD)) {
        continue;
      }

      // Preserve board state
      position_t pos_copy = *pos;

      // Increment ply
      pos->ply++;

      // Increment repetition index & store hash key
      thread->repetition_index++;
      thread->repetition_table[thread->repetition_index] =
          pos->hash_keys.hash_key;

      // Make sure to make only legal moves
      if (make_move(pos, move) == 0) {
        pos->ply--;
        thread->repetition_index--;
        continue;
      }

      calculate_threats(&pos_copy, ss + 1);
      update_nnue(pos, thread, pos_copy.mailbox, move);

      ss->move = move;
      ss->piece = pos_copy.mailbox[get_move_source(move)];

      thread->nodes++;

      prefetch_hash_entry(pos->hash_keys.hash_key);

      // Shallow search with raised beta
      int16_t probcut_score = -quiescence(pos, thread, ss + 1, -probcut_beta,
                                          -probcut_beta + 1, NON_PV);

      // If qsearch doesn't fail high, try a deeper search
      if (probcut_score >= probcut_beta) {
        probcut_score =
            -negamax(pos, thread, ss + 1, -probcut_beta, -probcut_beta + 1,
                     probcut_depth, !cutnode, NON_PV);
      }

      // Restore position
      pos->ply--;
      thread->repetition_index--;
      *pos = pos_copy;

      // Check if we need to stop
      if (thread->stopped == 1) {
        return 0;
      }

      // If shallow search failed high, we can prune
      if (probcut_score >= probcut_beta) {
        // Store in transposition table
        write_hash_entry(tt_entry, pos, probcut_score, raw_static_eval,
                         probcut_depth, move, HASH_FLAG_LOWER_BOUND, ss->tt_pv);
        return probcut_score;
      }
    }
  }

  // Internal Iterative Reductions
  if ((pv_node || cutnode) && !ss->excluded_move && depth >= IIR_DEPTH &&
      (!tt_move || tt_depth < depth - IIR_DEPTH_REDUCTION)) {
    depth--;
  }

  // create move list instance
  moves move_list[1];
  moves quiet_list[1];
  moves capture_list[1];
  quiet_list->count = 0;
  capture_list->count = 0;

  // generate moves
  generate_moves(pos, move_list);

  int16_t best_score = NO_SCORE;
  current_score = NO_SCORE;

  uint16_t best_move = 0;
  for (uint32_t count = 0; count < move_list->count; count++) {
    score_move(pos, thread, ss, &move_list->entry[count], tt_move);
  }

  uint8_t skip_quiets = 0;

  const int16_t original_alpha = alpha;

  uint16_t move_index = 0;

  // loop over moves within a movelist
  while (move_index < move_list->count) {
    uint16_t move = pick_next_best_move(move_list, &move_index).move;
    uint8_t quiet =
        (get_move_capture(move) == 0 && is_move_promotion(move) == 0);

    if (move == ss->excluded_move) {
      continue;
    }

    if (skip_quiets && quiet) {
      continue;
    }

    ss->history_score =
        quiet
            ? thread->quiet_history[pos->side][get_move_source(move)]
                                   [get_move_target(move)][is_square_threatened(
                                       ss, get_move_source(move))]
                                   [is_square_threatened(
                                       ss, get_move_target(move))] *
                      SEARCH_QUIET_HIST_MULT +
                  get_conthist_score(thread, pos, ss, move, 1) *
                      SEARCH_CONT1_HIST_MULT +
                  get_conthist_score(thread, pos, ss, move, 2) *
                      SEARCH_CONT2_HIST_MULT
            : thread->capture_history[pos->mailbox[get_move_source(move)]]
                                     [pos->mailbox[get_move_target(move)]]
                                     [get_move_source(move)]
                                     [get_move_target(move)] *
                      SEARCH_CAPT_HIST_MULT +
                  mvv[pos->mailbox[get_move_target(move)] % 6] *
                      SEARCH_MVV_MULT;
    ss->history_score /= 1024;

    // Late Move Pruning
    if (!pv_node && quiet &&
        moves_seen >=
            LMP_MARGIN[initial_depth][improving || ss->static_eval >= beta] &&
        !only_pawns(pos)) {
      skip_quiets = 1;
    }

    int r = lmr[quiet][MIN(63, depth)][MIN(63, moves_seen)];
    r += !pv_node;
    int lmr_depth = MAX(1, depth - 1 - MAX(r, 1));
    // Futility Pruning
    if (!root_node && current_score > -MATE_SCORE && lmr_depth <= FP_DEPTH &&
        !in_check && quiet &&
        ss->static_eval + lmr_depth * FP_MULTIPLIER + FP_ADDITION +
                ss->history_score / FP_HISTORY_DIVISOR <=
            alpha &&
        !might_give_check(pos, move)) {
      skip_quiets = 1;
      continue;
    }

    // SEE PVS Pruning
    if (depth <= SEE_DEPTH && moves_seen > 0 &&
        !SEE(pos, move,
             SEE_MARGIN[depth][!get_move_capture(move)] -
                 ss->history_score / SEE_HISTORY_DIVISOR))
      continue;

    int extensions = 0;

    // Singular Extensions
    // A rather simple idea that if our TT move is accurate we run a reduced
    // search to see if we can beat this score. If not we extend the TT move
    // search
    if (pos->ply < thread->depth * 2 && !root_node && depth >= SE_DEPTH &&
        move == tt_move && !ss->excluded_move &&
        tt_depth >= depth - SE_DEPTH_REDUCTION &&
        tt_flag != HASH_FLAG_UPPER_BOUND && abs(tt_score) < MATE_SCORE) {
      const int s_beta = tt_score - depth;
      const int s_depth = 3 * (depth - 1) / 8;

      position_t pos_copy = *pos;

      if (make_move(&pos_copy, move) == 0) {
        continue;
      }

      ss->excluded_move = move;

      const int16_t s_score = negamax(pos, thread, ss, s_beta - 1, s_beta,
                                      s_depth, cutnode, NON_PV);

      ss->excluded_move = 0;

      // No move beat tt score so we extend the search
      if (s_score < s_beta) {
        extensions++;
        if (s_score < s_beta - SE_PV_DOUBLE_MARGIN * pv_node) {
          extensions++;
          if (!get_move_capture(move) && s_score + SE_TRIPLE_MARGIN < s_beta) {
            extensions++;
          }
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

    // preserve board state
    position_t pos_copy = *pos;

    // increment ply
    pos->ply++;

    // increment repetition index & store hash key
    thread->repetition_index++;
    thread->repetition_table[thread->repetition_index] =
        pos->hash_keys.hash_key;

    // make sure to make only legal moves
    if (make_move(pos, move) == 0) {
      // decrement ply
      pos->ply--;

      // decrement repetition index
      thread->repetition_index--;

      // skip to next move
      continue;
    }

    calculate_threats(pos, ss + 1);

    update_nnue(pos, thread, pos_copy.mailbox, move);

    ss->move = move;
    ss->piece = pos_copy.mailbox[get_move_source(move)];

    // increment nodes count
    thread->nodes++;

    // increment legal moves
    moves_seen++;

    if (quiet) {
      add_move(quiet_list, move);

    } else {
      add_move(capture_list, move);
    }

    prefetch_hash_entry(pos->hash_keys.hash_key);

    uint64_t nodes_before_search = thread->nodes;

    // PVS & LMR
    int new_depth = depth + extensions - 1;

    // LMR
    if (depth >= 2 && moves_seen > 2 + 2 * pv_node) {
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
      R -= stm_in_check(pos) * LMR_IN_CHECK;
      R += (ss->cutoff_cnt > 3) * LMR_CUTOFF_CNT;
      R -= improving * LMR_IMPROVING;

      ss->reduction = R;

      R = R / 1024;
      int reduced_depth =
          MAX(1, MIN(new_depth - R, new_depth + cutnode)) + pv_node;

      current_score = -negamax(pos, thread, ss + 1, -alpha - 1, -alpha,
                               reduced_depth, 1, NON_PV);
      ss->reduction = 0;

      if (current_score > alpha && R != 0) {
        new_depth += (current_score > best_score + LMR_DEEPER_MARGIN +
                                          round(LMR_DEEPER_MULT * new_depth));
        new_depth -= (current_score < best_score + LMR_SHALLOWER_MARGIN);

        if (new_depth > reduced_depth) {
          current_score = -negamax(pos, thread, ss + 1, -alpha - 1, -alpha,
                                   new_depth, !cutnode, NON_PV);
        }
      }
      // Full Depth Search
    } else if (!pv_node || moves_seen > 1) {
      current_score = -negamax(pos, thread, ss + 1, -alpha - 1, -alpha,
                               new_depth, !cutnode, NON_PV);
    }

    // Principal Variation Search
    if (pv_node && (moves_seen == 1 || current_score > alpha)) {
      current_score =
          -negamax(pos, thread, ss + 1, -beta, -alpha, new_depth, 0, PV_NODE);
    }

    // decrement ply
    pos->ply--;

    // decrement repetition index
    thread->repetition_index--;

    // take move back
    *pos = pos_copy;

    if (thread->index == 0 && root_node) {
      nodes_spent_table[move >> 4] += thread->nodes - nodes_before_search;
    }

    // return INF so we can deal with timeout in case we are doing
    // re-search
    if (thread->stopped == 1) {
      return 0;
    }

    // found a better move
    if (current_score > best_score) {
      best_score = current_score;
      if (current_score > alpha) {
        best_move = move;

        // PV node (position)
        alpha = current_score;

        // write PV move
        thread->pv.pv_table[pos->ply][pos->ply] = move;

        // loop over the next ply
        for (int next_ply = pos->ply + 1;
             next_ply < thread->pv.pv_length[pos->ply + 1]; next_ply++)
          // copy move from deeper ply into a current ply's line
          thread->pv.pv_table[pos->ply][next_ply] =
              thread->pv.pv_table[pos->ply + 1][next_ply];

        // adjust PV length
        thread->pv.pv_length[pos->ply] = thread->pv.pv_length[pos->ply + 1];

        // fail-hard beta cutoff
        if (alpha >= beta) {
          // on quiet moves
          if (quiet) {
            update_quiet_histories(thread, pos, ss, quiet_list, best_move,
                                   depth);
          }

          update_capture_history_moves(thread, pos, capture_list, best_move,
                                       depth);
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
      return -MATE_VALUE + pos->ply;

    // king is not in check
    else
      // return stalemate score
      return 0;
  }

  if (best_score >= beta && abs(best_score) < MATE_SCORE &&
      abs(beta) < MATE_SCORE) {
    best_score = (best_score * depth + beta) / (depth + 1);
  }

  if (!ss->excluded_move) {
    uint8_t hash_flag = HASH_FLAG_EXACT;
    if (alpha >= beta) {
      hash_flag = HASH_FLAG_LOWER_BOUND;
    } else if (alpha <= original_alpha) {
      hash_flag = HASH_FLAG_UPPER_BOUND;
    }
    // store hash entry with the score equal to alpha
    write_hash_entry(tt_entry, pos, best_score, raw_static_eval, depth,
                     best_move, hash_flag, ss->tt_pv);

    if (!in_check &&
        (!best_move ||
         !(get_move_capture(best_move) || is_move_promotion(best_move))) &&
        (hash_flag != HASH_FLAG_LOWER_BOUND || best_score > raw_static_eval) &&
        (hash_flag != HASH_FLAG_UPPER_BOUND || best_score <= raw_static_eval)) {
      update_corrhist(thread, pos, raw_static_eval, best_score, depth);
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
    printf("cp %d ", 100 * score / 215);
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

static inline uint8_t aspiration_windows(thread_t *thread, position_t *pos,
                                         searchstack_t *ss, int16_t alpha,
                                         int16_t beta) {
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
    thread->score = negamax(pos, thread, ss + 7, alpha, beta,
                            thread->depth - fail_high_count, 0, PV_NODE);

    // We hit an apspiration window cut-off before time ran out and we jumped
    // to another depth with wider search which we didnt finish
    if (thread->stopped) {
      return 1;
    }

    if (thread->score <= alpha) {
      beta = (alpha + beta) / 2;

      alpha = MAX(-INF, alpha - window);
      fail_high_count = 0;
    }

    else if (thread->score >= beta) {
      beta = MIN(INF, beta + window);

      if (alpha < 2000) {
        ++fail_high_count;
      }
    } else {
      break;
    }

    window += ASP_WINDOW_MULTIPLIER * window / 1024;
  }
  return 0;
}

void *iterative_deepening(void *thread_void) {
  thread_t *thread = (thread_t *)thread_void;
  position_t *pos = &thread->pos;

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

    if (aspiration_windows(thread, pos, ss, alpha, beta)) {
      return NULL;
    }

    if (thread->index == 0) {
      average_score = average_score == NO_SCORE
                          ? thread->score
                          : (average_score + thread->score) / 2;

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

    if (thread->index == 0) {
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
void search_position(position_t *pos, thread_t *threads) {
  pthread_t pthreads[thread_count];
  for (int i = 0; i < thread_count; ++i) {
    threads[i].nodes = 0;
    threads[i].stopped = 0;
    memcpy(&threads[i].pos, pos, sizeof(position_t));
    init_accumulator(pos, threads[i].accumulator);
    init_finny_tables(&threads[i], pos);
  }

  // clear helper data structures for search
  memset(threads->pv.pv_table, 0, sizeof(threads->pv.pv_table));
  memset(threads->pv.pv_length, 0, sizeof(threads->pv.pv_length));
  memset(nodes_spent_table, 0, sizeof(nodes_spent_table));

  for (int thread_index = 1; thread_index < thread_count; ++thread_index) {
    pthread_create(&pthreads[thread_index], NULL, &iterative_deepening,
                   &threads[thread_index]);
  }

  iterative_deepening(&threads[0]);

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
