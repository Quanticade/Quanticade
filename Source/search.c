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

int LMP_BASE = 2;
int RAZOR_DEPTH = 7;
int RAZOR_MARGIN = 313;
int RFP_DEPTH = 7;
int RFP_MARGIN = 54;
int FP_DEPTH = 10;
int FP_MULTIPLIER = 172;
int FP_ADDITION = 168;
int NMP_BASE_REDUCTION = 4;
int NMP_DIVISER = 3;
int NMP_RED_DIVISER = 205;
int NMP_RED_MIN = 6;
int NMP_DEPTH = 3;
int IIR_DEPTH = 4;
int IIR_DEPTH_REDUCTION = 4;
int SEE_QUIET = 63;
int SEE_CAPTURE = 34;
int SEE_DEPTH = 10;
int SEE_HISTORY_DIVISOR = 60;
int SE_DEPTH = 6;
int SE_DEPTH_REDUCTION = 4;
int SE_PV_DOUBLE_MARGIN = 2;
int SE_TRIPLE_MARGIN = 35;
int LMR_PV_NODE = 1213;
int LMR_HISTORY_QUIET = 1159;
int LMR_HISTORY_NOISY = 1256;
int LMR_IN_CHECK = 894;
int LMR_CUTNODE = 964;
int LMR_TT_DEPTH = 1014;
int LMR_TT_PV = 986;
int LMR_TT_SCORE = 1024;
int LMR_DEEPER_MARGIN = 35;
int LMR_SHALLOWER_MARGIN = 6;
int LMP_DEPTH_DIVISOR = 3;
int ASP_WINDOW = 11;
int ASP_DEPTH = 4;
int QS_SEE_THRESHOLD = 6;
int MO_SEE_THRESHOLD = 126;
double ASP_MULTIPLIER = 1.6997510023971298;
int LMR_QUIET_HIST_DIV = 7519;
int LMR_CAPT_HIST_DIV = 7453;
double LMR_OFFSET_QUIET = 0.8399286894506875;
double LMR_DIVISOR_QUIET = 1.49112080246623;
double LMR_OFFSET_NOISY = -0.1697125002863307;
double LMR_DIVISOR_NOISY = 3.0235493101143325;

double NODE_TIME_MULTIPLIER = 2.3266269428013517;
double NODE_TIME_ADDITION = 0.44433971476089684;
double NODE_TIME_MIN = 0.550444231059761;

int mvv[] = {124, 367, 267, 629, 1422, 0};

int SEEPieceValues[] = {80, 294, 294, 549, 983, 0, 0};

int lmr[2][MAX_PLY + 1][256];

int SEE_MARGIN[MAX_PLY + 1][2];

double bestmove_scale[5] = {2.4127778879395403, 1.3591679728822201,
                            1.0892878736366167, 0.8801589058035711,
                            0.6928914388892039};
double eval_scale[5] = {1.2422734971107077, 1.1390735152797768,
                        0.9904722958613691, 0.945176041488196,
                        0.8876516599534069};

uint64_t nodes_spent_table[4096] = {0};

// Initializes the late move reduction array
void init_reductions(void) {
  for (int depth = 0; depth <= MAX_PLY; depth++) {
    SEE_MARGIN[depth][0] = -SEE_CAPTURE * depth * depth;
    SEE_MARGIN[depth][1] = -SEE_QUIET * depth;
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
  limits.soft_limit =
      MIN(thread->starttime +
              limits.base_soft * bestmove_scale[best_move_stability] *
                  eval_scale[eval_stability] * node_scaling_factor,
          limits.max_time + thread->starttime);
}

uint8_t check_time(thread_t *thread) {
  // if time is up break here
  if (thread->index == 0 &&
      ((limits.timeset && get_time_ms() > limits.hard_limit) ||
       (limits.nodes_set && thread->nodes >= limits.node_limit))) {
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
  uint8_t piece = get_move_promoted(pos->side, move);

  if (move == hash_move) {
    move_entry->score = 2000000000;
    return;
  }

  if (piece) {
    // We have a promotion
    switch (piece) {
    case q:
    case Q:
      move_entry->score = 1400000001;
      break;
    case n:
    case N:
      move_entry->score = 1400000000;
      break;
    default:
      move_entry->score = -1000000;
      break;
    }
    if (get_move_capture(move)) {
      // The promotion is a capture and we check SEE score
      if (SEE(pos, move, -MO_SEE_THRESHOLD)) {
        return;
      } else {
        // Capture failed SEE and thus gets ordered at the bottom of the list
        move_entry->score = -1000000;
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
    int target_piece = P;

    uint8_t bb_piece = pos->mailbox[get_move_target(move)];
    // if there's a piece on the target square
    if (bb_piece != NO_PIECE &&
        get_bit(pos->bitboards[bb_piece], get_move_target(move))) {
      target_piece = bb_piece;
    }

    // score move by MVV LVA lookup [source piece][target piece]
    move_entry->score +=
        mvv[target_piece > 5 ? target_piece - 6 : target_piece];
    move_entry->score +=
        thread
            ->capture_history[pos->mailbox[get_move_source(move)]][target_piece]
                             [get_move_source(move)][get_move_target(move)];
    move_entry->score +=
        SEE(pos, move, -MO_SEE_THRESHOLD) ? 1000000000 : -1000000;
    return;
  }

  // score quiet move
  else {
    // score 1st killer move
    if (thread->killer_moves[pos->ply] == move) {
      move_entry->score = 900000000;
    }

    // score history move
    else {
      move_entry->score =
          thread->quiet_history[pos->mailbox[get_move_source(move)]]
                               [get_move_source(move)][get_move_target(move)] +
          get_conthist_score(thread, ss - 1, move) +
          get_conthist_score(thread, ss - 2, move) +
          get_conthist_score(thread, ss - 4, move) +
          thread->pawn_history[pos->hash_keys.pawn_key % 32767]
                              [pos->mailbox[get_move_source(move)]]
                              [get_move_target(move)];
    }

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
  if (pos->ply > MAX_PLY - 1) {
    // evaluate position
    return evaluate(pos, &thread->accumulator[pos->ply]);
  }

  if (pos->ply > thread->seldepth) {
    thread->seldepth = pos->ply;
  }

  uint16_t best_move = 0;
  int16_t score = NO_SCORE, best_score = NO_SCORE;
  int16_t raw_static_eval = NO_SCORE;
  int16_t tt_score = NO_SCORE;
  int16_t tt_static_eval = NO_SCORE;
  uint8_t tt_hit = 0;
  uint8_t tt_flag = HASH_FLAG_EXACT;
  uint8_t tt_was_pv = pv_node;

  tt_entry_t *tt_entry = read_hash_entry(pos, &tt_hit);

  if (tt_hit) {
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

  raw_static_eval = tt_static_eval != NO_SCORE
                        ? tt_static_eval
                        : evaluate(pos, &thread->accumulator[pos->ply]);
  ss->static_eval = adjust_static_eval(thread, pos, raw_static_eval);

  if (tt_hit && can_use_score(best_score, best_score, tt_score, tt_flag)) {
    best_score = tt_score;
  } else {
    best_score = ss->static_eval;
  }

  // fail-hard beta cutoff
  if (best_score >= beta) {
    if (abs(best_score) < MATE_SCORE && abs(beta) < MATE_SCORE) {
      best_score = (best_score + beta) / 2;
    }
    // node (position) fails high
    return best_score;
  }

  // found a better move
  alpha = MAX(alpha, best_score);

  // create move list instance
  moves move_list[1];
  moves capture_list[1];
  capture_list->count = 0;

  // generate moves
  generate_captures(pos, move_list);

  for (uint32_t count = 0; count < move_list->count; count++) {
    score_move(pos, thread, ss, &move_list->entry[count], best_move);
  }

  uint16_t move_index = 0;

  // loop over moves within a movelist

  while (move_index < move_list->count) {
    uint16_t move = pick_next_best_move(move_list, &move_index).move;

    if (!SEE(pos, move, -QS_SEE_THRESHOLD))
      continue;

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

    update_nnue(pos, thread, pos_copy.mailbox, move);

    ss->move = move;
    ss->piece = pos_copy.mailbox[get_move_source(move)];

    thread->nodes++;

    if (get_move_capture(move)) {
      add_move(capture_list, move);
    }

    prefetch_hash_entry(pos->hash_keys.hash_key);

    // score current move
    score = -quiescence(pos, thread, ss + 1, -beta, -alpha, pv_node);

    // decrement ply
    pos->ply--;

    // decrement repetition index
    thread->repetition_index--;

    // take move back
    *pos = pos_copy;

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
          update_capture_history_moves(thread, capture_list, best_move, 1);
          break;
        }
      }
    }
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
  // init PV length
  thread->pv.pv_length[pos->ply] = pos->ply;

  // variable to store current move's score (from the static evaluation
  // perspective)
  int16_t current_score = NO_SCORE, static_eval = NO_SCORE;
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
  depth = MIN(depth, MAX_PLY - 1);

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

    // we are too deep, hence there's an overflow of arrays relying on max ply
    // constant
    if (pos->ply > MAX_PLY - 1) {
      // evaluate position
      return evaluate(pos, &thread->accumulator[pos->ply]);
    }

    // Mate distance pruning
    alpha = MAX(alpha, -MATE_VALUE + (int)pos->ply);
    beta = MIN(beta, MATE_VALUE - (int)pos->ply - 1);
    if (alpha >= beta)
      return alpha;
  }

  // is king in check
  uint8_t in_check = is_square_attacked(
      pos,
      (pos->side == white) ? __builtin_ctzll(pos->bitboards[K])
                           : __builtin_ctzll(pos->bitboards[k]),
      pos->side ^ 1);

  // recursion escape condition
  if (!in_check && depth <= 0) {
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
    return tt_score;
  }

  // Internal Iterative Reductions
  if ((pv_node || cutnode) && !ss->excluded_move && depth >= IIR_DEPTH &&
      (!tt_move || tt_depth < depth - IIR_DEPTH_REDUCTION)) {
    depth--;
  }

  if (in_check) {
    static_eval = ss->static_eval = NO_SCORE;
  } else if (!ss->excluded_move) {
    raw_static_eval = tt_static_eval != NO_SCORE
                          ? tt_static_eval
                          : evaluate(pos, &thread->accumulator[pos->ply]);

    // adjust static eval with corrhist
    static_eval = ss->static_eval =
        adjust_static_eval(thread, pos, raw_static_eval);

    if (tt_hit && can_use_score(static_eval, static_eval, tt_score, tt_flag)) {
      ss->eval = tt_score;
    } else {
      ss->eval = ss->static_eval;
    }
  }

  uint8_t improving = 0;
  uint8_t opponent_worsening = 0;

  if ((ss - 2)->static_eval != NO_SCORE) {
    improving = ss->static_eval > (ss - 2)->static_eval;
  }
  if (!in_check) {
    opponent_worsening = ss->static_eval + (ss - 1)->static_eval > 1;
  }

  // Check on time
  if (check_time(thread)) {
    stop_threads(thread, thread_count);
    return 0;
  }

  // moves seen counter
  uint16_t moves_seen = 0;

  if (!pv_node && !in_check && !ss->excluded_move) {
    if ((ss - 1)->reduction >= 3 && !opponent_worsening) {
      ++depth;
    }
    // Reverse Futility Pruning
    if (depth <= RFP_DEPTH) {
      // get static evaluation score

      // define evaluation margin
      uint16_t eval_margin = RFP_MARGIN * MAX(0, depth - improving);

      // evaluation margin substracted from static evaluation score fails high
      if (ss->eval - eval_margin >= beta)
        // evaluation margin substracted from static evaluation score
        return beta + (ss->eval - beta) / 3;
    }

    // null move pruning
    if (!ss->null_move && ss->eval >= beta && depth >= NMP_DEPTH && !only_pawns(pos)) {
      int R = MIN((ss->eval - beta) / NMP_RED_DIVISER, NMP_RED_MIN) +
              depth / NMP_DIVISER + NMP_BASE_REDUCTION;
      R = MIN(R, depth);
      // preserve board state
      position_t pos_copy = *pos;

      thread->accumulator[pos->ply + 1] = thread->accumulator[pos->ply];

      // increment ply
      pos->ply++;

      // increment repetition index & store hash key
      thread->repetition_index++;
      thread->repetition_table[thread->repetition_index] =
          pos->hash_keys.hash_key;

      // hash enpassant if available
      if (pos->enpassant != no_sq)
        pos->hash_keys.hash_key ^= keys.enpassant_keys[pos->enpassant];

      // reset enpassant capture square
      pos->enpassant = no_sq;

      // switch the side, literally giving opponent an extra move to make
      pos->side ^= 1;

      // hash the side
      pos->hash_keys.hash_key ^= keys.side_key;

      prefetch_hash_entry(pos->hash_keys.hash_key);

      ss->move = 0;
      ss->piece = 0;
      (ss + 1)->null_move = 1;

      /* search moves with reduced depth to find beta cutoffs
         depth - 1 - R where R is a reduction limit */
      current_score = -negamax(pos, thread, ss + 1, -beta, -beta + 1, depth - R,
                               !cutnode, NON_PV);

      (ss + 1)->null_move = 0;

      // decrement ply
      pos->ply--;

      // decrement repetition index
      thread->repetition_index--;

      // restore board state
      *pos = pos_copy;

      // return 0 if time is up
      if (thread->stopped == 1) {
        return 0;
      }

      // fail-hard beta cutoff
      if (current_score >= beta)
        // node (position) fails high
        return current_score;
    }

    if (depth <= RAZOR_DEPTH &&
        ss->static_eval + RAZOR_MARGIN * depth < alpha) {
      const int16_t razor_score =
          quiescence(pos, thread, ss, alpha, beta, NON_PV);
      if (razor_score <= alpha) {
        return razor_score;
      }
    }
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
        quiet ? thread->quiet_history[pos->mailbox[get_move_source(move)]]
                                     [get_move_source(move)]
                                     [get_move_target(move)] +
                    get_conthist_score(thread, ss - 1, move) +
                    get_conthist_score(thread, ss - 2, move)
              : thread->capture_history[pos->mailbox[get_move_source(move)]]
                                       [pos->mailbox[get_move_target(move)]]
                                       [get_move_source(move)]
                                       [get_move_target(move)];

    // Late Move Pruning
    if (!pv_node && quiet &&
        moves_seen >= LMP_BASE + depth * depth / (LMP_DEPTH_DIVISOR - improving) &&
        !only_pawns(pos)) {
      skip_quiets = 1;
    }

    int r = lmr[quiet][MIN(63, depth)][MIN(63, moves_seen)];
    r += !pv_node;
    int lmr_depth = MAX(1, depth - 1 - MAX(r, 1));

    // Futility Pruning
    if (!root_node && current_score > -MATE_SCORE && lmr_depth <= FP_DEPTH &&
        !in_check && quiet &&
        ss->static_eval + lmr_depth * FP_MULTIPLIER + FP_ADDITION <= alpha) {
      skip_quiets = 1;
      continue;
    }

    // SEE PVS Pruning
    if (depth <= SEE_DEPTH && moves_seen > 0 &&
        !SEE(pos, move,
             SEE_MARGIN[depth][quiet] -
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
      const int s_depth = (depth - 1) / 2;

      position_t pos_copy = *pos;

      if (make_move(pos, move) == 0) {
        continue;
      }

      *pos = pos_copy;

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

    if (in_check) {
      extensions++;
    }

    uint64_t nodes_before_search = thread->nodes;

    // PVS & LMR
    int new_depth = depth + extensions - 1;

    int R = lmr[quiet][depth][MIN(255, moves_seen)] * 1024;
    R += !pv_node * LMR_PV_NODE;
    R -= ss->history_score * (quiet ? LMR_HISTORY_QUIET : LMR_HISTORY_NOISY) /
         (quiet ? LMR_QUIET_HIST_DIV : LMR_CAPT_HIST_DIV);
    R -= in_check * LMR_IN_CHECK;
    R += cutnode * LMR_CUTNODE;
    R -= (tt_depth >= depth) * LMR_TT_DEPTH;
    R -= ss->tt_pv * LMR_TT_PV;
    R += (ss->tt_pv && tt_hit && tt_entry->score <= alpha) * LMR_TT_SCORE;
    R = R / 1024;
    int reduced_depth = MAX(1, MIN(new_depth - R, new_depth));

    if (depth >= 2 && moves_seen > 2 + 2 * pv_node) {
      ss->reduction = R;
      current_score = -negamax(pos, thread, ss + 1, -alpha - 1, -alpha,
                               reduced_depth, 1, NON_PV);
      ss->reduction = 0;

      if (current_score > alpha && R != 0) {
        new_depth +=
            (current_score > best_score + LMR_DEEPER_MARGIN + 2 * new_depth);
        new_depth -= (current_score < best_score + LMR_SHALLOWER_MARGIN);

        current_score = -negamax(pos, thread, ss + 1, -alpha - 1, -alpha,
                                 new_depth, !cutnode, NON_PV);
      }
    } else if (!pv_node || moves_seen > 1) {
      current_score = -negamax(pos, thread, ss + 1, -alpha - 1, -alpha,
                               new_depth, !cutnode, NON_PV);
    }

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
            update_quiet_history_moves(thread, quiet_list, best_move, depth);
            update_continuation_history_moves(thread, ss, quiet_list, best_move,
                                              depth);
            update_pawn_history_moves(thread, quiet_list, best_move, depth);
            thread->killer_moves[pos->ply] = move;
          }

          update_capture_history_moves(thread, capture_list, best_move, depth);
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

    if (!in_check && (!best_move || !(is_move_promotion(best_move) ||
                                      get_move_capture(best_move)))) {
      update_pawn_corrhist(thread, raw_static_eval, best_score, depth, tt_flag);
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
      alpha = MAX(-INF, thread->score - window);
      beta = MIN(INF, thread->score + window);
    }

    // find best move within a given position
    thread->score = negamax(pos, thread, ss + 4, alpha, beta,
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

      if (alpha < 2000 && fail_high_count < 2) {
        ++fail_high_count;
      }
    } else {
      break;
    }

    window *= ASP_MULTIPLIER;
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

    searchstack_t ss[MAX_PLY + 4];
    for (int i = 0; i < MAX_PLY + 4; ++i) {
      ss[i].excluded_move = 0;
      ss[i].static_eval = NO_SCORE;
      ss[i].eval = NO_SCORE;
      ss[i].history_score = 0;
      ss[i].move = 0;
      ss[i].piece = 0;
      ss[i].null_move = 0;
      ss[i].reduction = 0;
      ss[i].tt_pv = 0;
    }

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

      if (thread->score > average_score - 10 &&
          thread->score < average_score + 10) {
        eval_stability = MIN(eval_stability + 1, 4);
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
         (limits.nodes_set && thread->nodes >= limits.node_limit))) {
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
    memset(threads[i].killer_moves, 0, sizeof(threads[i].killer_moves));
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
