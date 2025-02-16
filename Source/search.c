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

int LMP_BASE = 3;
int LMP_MULTIPLIER = 1;
int RAZOR_DEPTH = 7;
int RAZOR_MARGIN = 324;
int RFP_DEPTH = 7;
int RFP_MARGIN = 53;
int FP_DEPTH = 5;
int FP_MULTIPLIER = 178;
int FP_ADDITION = 159;
int NMP_BASE_REDUCTION = 4;
int NMP_DIVISER = 3;
int NMP_RED_DIVISER = 205;
int NMP_RED_MIN = 6;
int IIR_DEPTH = 4;
int SEE_QUIET = 63;
int SEE_CAPTURE = 34;
int SEE_DEPTH = 10;
int SE_DEPTH = 6;
int SE_DEPTH_REDUCTION = 4;
int SE_TRIPLE_MARGIN = 35;
int LMR_PV_NODE = 1192;
int LMR_HISTORY = 1170;
int LMR_IN_CHECK = 902;
int LMR_CUTNODE = 877;
int LMR_TT_DEPTH = 991;
int ASP_WINDOW = 11;
int ASP_DEPTH = 4;
int QS_SEE_THRESHOLD = 6;
int MO_SEE_THRESHOLD = 130;
double ASP_MULTIPLIER = 1.6541989293231878;
int LMR_QUIET_HIST_DIV = 7323;
int LMR_CAPT_HIST_DIV = 7534;
double LMR_OFFSET_QUIET = 0.8500446673974761;
double LMR_DIVISOR_QUIET = 1.4141416976120227;
double LMR_OFFSET_NOISY = -0.23601909184212566;
double LMR_DIVISOR_NOISY = 2.8963234699203277;

int mvv[] = {122, 387, 314, 569, 1464, 0};

int SEEPieceValues[] = {98, 285, 304, 518, 925, 0, 0};

int lmr[2][MAX_PLY + 1][256];

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
    if (get_move_capture(move) && SEE(pos, move, -MO_SEE_THRESHOLD)) {
      return;
    } else {
      move_entry->score = -1000000;
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
          get_conthist_score(thread, ss - 4, move);
    }

    return;
  }

  move_entry->score = 0;
  return;
}

// sort moves in descending order
static inline void sort_moves(moves *move_list) {
  for (uint32_t i = 1; i < move_list->count; i++) {
    move_t key = move_list->entry[i];
    int j = i - 1;

    // Sort in descending order by score
    while (j >= 0 && move_list->entry[j].score < key.score) {
      move_list->entry[j + 1] = move_list->entry[j];
      j--;
    }
    move_list->entry[j + 1] = key;
  }
}

// position repetition detection
static inline int is_repetition(position_t *pos) {
  // loop over repetition indices range
  for (uint32_t index = 0; index < pos->repetition_index; index++)
    // if we found the hash key same with a current
    if (pos->repetition_table[index] == pos->hash_key)
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
static inline int quiescence(position_t *pos, thread_t *thread,
                             searchstack_t *ss, int alpha, int beta) {
  // Check on time
  if (check_time(thread)) {
    stop_threads(thread, thread_count);
    return 0;
  }

  // we are too deep, hence there's an overflow of arrays relying on max ply
  // constant
  if (pos->ply > MAX_PLY - 1)
    // evaluate position
    return evaluate(pos, &thread->accumulator[pos->ply]);
  ;

  if (pos->ply > pos->seldepth) {
    pos->seldepth = pos->ply;
  }

  int32_t best_move = 0;
  int score, best_score = 0;
  int pv_node = beta - alpha > 1;
  int16_t tt_score = 0;
  uint8_t tt_hit = 0;
  uint8_t tt_depth = 0;
  uint8_t tt_flag = HASH_FLAG_EXACT;

  if (pos->ply &&
      (tt_hit =
           read_hash_entry(pos, &best_move, &tt_score, &tt_depth, &tt_flag)) &&
      pv_node == 0) {
    if ((tt_flag == HASH_FLAG_EXACT) ||
        ((tt_flag == HASH_FLAG_UPPER_BOUND) && (tt_score <= alpha)) ||
        ((tt_flag == HASH_FLAG_LOWER_BOUND) && (tt_score >= beta))) {
      return tt_score;
    }
  }

  // evaluate position
  score = best_score =
      tt_hit ? tt_score : evaluate(pos, &thread->accumulator[pos->ply]);
  ;

  // fail-hard beta cutoff
  if (score >= beta) {
    // node (position) fails high
    return score;
  }

  // found a better move
  if (score > alpha) {
    // PV node (position)
    alpha = score;
  }

  // create move list instance
  moves move_list[1];
  moves capture_list[1];
  capture_list->count = 0;

  // generate moves
  generate_captures(pos, move_list);

  for (uint32_t count = 0; count < move_list->count; count++) {
    score_move(pos, thread, ss, &move_list->entry[count], best_move);
  }

  sort_moves(move_list);

  // loop over moves within a movelist
  for (uint32_t count = 0; count < move_list->count; count++) {

    if (!SEE(pos, move_list->entry[count].move, -QS_SEE_THRESHOLD))
      continue;

    // preserve board state
    copy_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
               pos->castle, pos->fifty, pos->hash_key, pos->mailbox);

    // increment ply
    pos->ply++;

    // increment repetition index & store hash key
    pos->repetition_index++;
    pos->repetition_table[pos->repetition_index] = pos->hash_key;

    // make sure to make only legal moves
    if (make_move(pos, move_list->entry[count].move, only_captures) == 0) {
      // decrement ply
      pos->ply--;

      // decrement repetition index
      pos->repetition_index--;

      // skip to next move
      continue;
    }

    accumulator_make_move(&thread->accumulator[pos->ply],
                          &thread->accumulator[pos->ply - 1], pos->side,
                          move_list->entry[count].move, mailbox_copy);

    ss->move = move_list->entry[count].move;
    ss->piece = mailbox_copy[get_move_source(move_list->entry[count].move)];

    thread->nodes++;

    if (!is_move_promotion(move_list->entry[count].move) ||
        !get_move_capture(move_list->entry[count].move)) {
      add_move(capture_list, move_list->entry[count].move);
    }

    prefetch_hash_entry(pos->hash_key);

    // score current move
    score = -quiescence(pos, thread, ss, -beta, -alpha);

    // decrement ply
    pos->ply--;

    // decrement repetition index
    pos->repetition_index--;

    // take move back
    restore_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
                  pos->castle, pos->fifty, pos->hash_key, pos->mailbox);

    // return 0 if time is up
    if (thread->stopped == 1) {
      return 0;
    }

    if (score > best_score) {
      best_score = score;
      best_move = move_list->entry[count].move;
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

  write_hash_entry(pos, best_score, 0, best_move, hash_flag);

  return best_score;
}

// negamax alpha beta search
static inline int negamax(position_t *pos, thread_t *thread, searchstack_t *ss,
                          int alpha, int beta, int depth, uint8_t cutnode) {
  // init PV length
  thread->pv.pv_length[pos->ply] = pos->ply;

  // variable to store current move's score (from the static evaluation
  // perspective)
  int current_score, static_eval = -INF;

  int tt_move = 0;
  int16_t tt_score = 0;
  uint8_t tt_hit = 0;
  uint8_t tt_depth = 0;
  uint8_t tt_flag = HASH_FLAG_EXACT;

  uint8_t root_node = pos->ply == 0;

  if (depth == 0 && pos->ply > pos->seldepth) {
    pos->seldepth = pos->ply;
  }

  if (!root_node) {
    // if position repetition occurs
    if (is_repetition(pos) || pos->fifty >= 100 || is_material_draw(pos)) {
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

  // a hack by Pedro Castro to figure out whether the current node is PV node
  // or not
  int pv_node = beta - alpha > 1;

  // is king in check
  int in_check = is_square_attacked(pos,
                                    (pos->side == white)
                                        ? __builtin_ctzll(pos->bitboards[K])
                                        : __builtin_ctzll(pos->bitboards[k]),
                                    pos->side ^ 1);

  // recursion escape condition
  if (!in_check && depth <= 0) {
    // run quiescence search
    return quiescence(pos, thread, ss, alpha, beta);
  }

  // read hash entry if we're not in a root ply and hash entry is available
  // and current node is not a PV node
  if (!ss->excluded_move &&
      (tt_hit =
           read_hash_entry(pos, &tt_move, &tt_score, &tt_depth, &tt_flag)) &&
      pv_node == 0 && !root_node) {
    if (tt_depth >= depth) {
      if ((tt_flag == HASH_FLAG_EXACT) ||
          ((tt_flag == HASH_FLAG_UPPER_BOUND) && (tt_score <= alpha)) ||
          ((tt_flag == HASH_FLAG_LOWER_BOUND) && (tt_score >= beta))) {
        return tt_score;
      }
    }
  }

  // Internal Iterative Reductions
  if ((pv_node || cutnode) && !ss->excluded_move && depth >= IIR_DEPTH &&
      !tt_move) {
    depth--;
  }

  if (!ss->excluded_move) {
    static_eval = ss->static_eval =
        in_check ? NO_SCORE
                 : (tt_hit ? tt_score
                           : evaluate(pos, &thread->accumulator[pos->ply]));
  }

  uint8_t improving = 0;

  if ((ss - 2)->static_eval != NO_SCORE) {
    improving = static_eval > (ss - 2)->static_eval;
  }

  // Check on time
  if (check_time(thread)) {
    stop_threads(thread, thread_count);
    return 0;
  }

  // legal moves counter
  int legal_moves = 0;

  if (!in_check && !ss->excluded_move) {
    // Reverse Futility Pruning
    if (depth <= RFP_DEPTH && !pv_node) {
      // get static evaluation score

      // define evaluation margin
      int eval_margin = RFP_MARGIN * depth;

      // evaluation margin substracted from static evaluation score fails high
      if (ss->static_eval - eval_margin >= beta)
        // evaluation margin substracted from static evaluation score
        return beta + (ss->static_eval - beta) / 3;
    }

    // null move pruning
    if (!ss->null_move && !pv_node && ss->static_eval >= beta && depth >= 3 &&
        !only_pawns(pos)) {
      int R = MIN((ss->static_eval - beta) / NMP_RED_DIVISER, NMP_RED_MIN) +
              depth / NMP_DIVISER + NMP_BASE_REDUCTION;
      R = MIN(R, depth);
      // preserve board state
      copy_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
                 pos->castle, pos->fifty, pos->hash_key, pos->mailbox);
      thread->accumulator[pos->ply + 1] = thread->accumulator[pos->ply];

      // increment ply
      pos->ply++;

      // increment repetition index & store hash key
      pos->repetition_index++;
      pos->repetition_table[pos->repetition_index] = pos->hash_key;

      // hash enpassant if available
      if (pos->enpassant != no_sq)
        pos->hash_key ^= keys.enpassant_keys[pos->enpassant];

      // reset enpassant capture square
      pos->enpassant = no_sq;

      // switch the side, literally giving opponent an extra move to make
      pos->side ^= 1;

      // hash the side
      pos->hash_key ^= keys.side_key;

      prefetch_hash_entry(pos->hash_key);

      ss->move = 0;
      ss->piece = 0;
      (ss + 1)->null_move = 1;

      /* search moves with reduced depth to find beta cutoffs
         depth - 1 - R where R is a reduction limit */
      current_score =
          -negamax(pos, thread, ss + 1, -beta, -beta + 1, depth - R, !cutnode);

      (ss + 1)->null_move = 0;

      // decrement ply
      pos->ply--;

      // decrement repetition index
      pos->repetition_index--;

      // restore board state
      restore_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
                    pos->castle, pos->fifty, pos->hash_key, pos->mailbox);

      // return 0 if time is up
      if (thread->stopped == 1) {
        return 0;
      }

      // fail-hard beta cutoff
      if (current_score >= beta)
        // node (position) fails high
        return current_score;
    }

    if (!pv_node && depth <= RAZOR_DEPTH &&
        ss->static_eval + RAZOR_MARGIN * depth < alpha) {
      const int razor_score = quiescence(pos, thread, ss, alpha, beta);
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

  int best_score = -INF;
  current_score = -INF;

  int best_move = 0;
  for (uint32_t count = 0; count < move_list->count; count++) {
    score_move(pos, thread, ss, &move_list->entry[count], tt_move);
  }

  sort_moves(move_list);

  uint8_t skip_quiets = 0;

  const int original_alpha = alpha;

  // loop over moves within a movelist
  for (uint32_t count = 0; count < move_list->count; count++) {
    int move = move_list->entry[count].move;
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
            ? thread
                  ->quiet_history[pos->mailbox[get_move_source(move)]]
                                 [get_move_source(move)][get_move_target(move)]
            : thread->capture_history[pos->mailbox[get_move_source(move)]]
                                     [pos->mailbox[get_move_target(move)]]
                                     [get_move_source(move)]
                                     [get_move_target(move)];

    // Late Move Pruning
    if (!root_node && quiet &&
        legal_moves >
            LMP_BASE + LMP_MULTIPLIER * depth * depth / (2 - improving) &&
        !only_pawns(pos)) {
      skip_quiets = 1;
    }

    int r = lmr[quiet][MIN(63, depth)][MIN(63, legal_moves)];
    int lmr_depth = MAX(1, depth - 1 - MAX(r, 1));

    // Futility Pruning
    if (!root_node && current_score > -MATE_SCORE && lmr_depth <= FP_DEPTH &&
        !in_check && quiet &&
        ss->static_eval + lmr_depth * FP_MULTIPLIER + FP_ADDITION <= alpha) {
      skip_quiets = 1;
      continue;
    }

    // SEE PVS Pruning
    const int see_threshold =
        quiet ? -SEE_QUIET * depth : -SEE_CAPTURE * depth * depth;
    if (depth <= SEE_DEPTH && legal_moves > 0 && !SEE(pos, move, see_threshold))
      continue;

    int extensions = 0;

    // Singular Extensions
    // A rather simple idea that if our TT move is accurate we run a reduced
    // search to see if we can beat this score. If not we extend the TT move
    // search
    if (!root_node && depth >= SE_DEPTH && move == tt_move &&
        !ss->excluded_move && tt_depth >= depth - SE_DEPTH_REDUCTION &&
        tt_flag != HASH_FLAG_UPPER_BOUND && abs(tt_score) < MATE_SCORE) {
      const int s_beta = tt_score - depth;
      const int s_depth = (depth - 1) / 2;

      copy_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
                 pos->castle, pos->fifty, pos->hash_key, pos->mailbox);

      if (make_move(pos, move, all_moves) == 0) {
        continue;
      }

      restore_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
                    pos->castle, pos->fifty, pos->hash_key, pos->mailbox);

      ss->excluded_move = move;

      const int16_t s_score =
          negamax(pos, thread, ss, s_beta - 1, s_beta, s_depth, cutnode);

      ss->excluded_move = 0;

      // No move beat tt score so we extend the search
      if (s_score < s_beta) {
        extensions++;
        if (s_score < s_beta - 2 * pv_node) {
          extensions++;
        }
        if (!get_move_capture(move) && s_score + SE_TRIPLE_MARGIN < s_beta) {
          extensions++;
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
    copy_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
               pos->castle, pos->fifty, pos->hash_key, pos->mailbox);

    // increment ply
    pos->ply++;

    // increment repetition index & store hash key
    pos->repetition_index++;
    pos->repetition_table[pos->repetition_index] = pos->hash_key;

    // make sure to make only legal moves
    if (make_move(pos, move, all_moves) == 0) {
      // decrement ply
      pos->ply--;

      // decrement repetition index
      pos->repetition_index--;

      // skip to next move
      continue;
    }

    accumulator_make_move(&thread->accumulator[pos->ply],
                          &thread->accumulator[pos->ply - 1], pos->side,
                          move_list->entry[count].move, mailbox_copy);

    ss->move = move;
    ss->piece = mailbox_copy[get_move_source(move)];

    // increment nodes count
    thread->nodes++;

    // increment legal moves
    legal_moves++;

    if (quiet) {
      add_move(quiet_list, move);

    } else {
      add_move(capture_list, move);
    }

    prefetch_hash_entry(pos->hash_key);

    uint8_t needs_full_search = 0;

    if (in_check) {
      extensions++;
    }

    // PVS & LMR
    int new_depth = depth + extensions - 1;

    if (depth > 1 && legal_moves > 2 + 2 * pv_node) {
      int R = lmr[quiet][depth][MIN(255, legal_moves)] * 1024;
      R += !pv_node * LMR_PV_NODE;
      R -= ss->history_score * LMR_HISTORY /
           (quiet ? LMR_QUIET_HIST_DIV : LMR_CAPT_HIST_DIV);
      R -= in_check * LMR_IN_CHECK;
      R += cutnode * LMR_CUTNODE;
      R -= (tt_depth >= depth) * LMR_TT_DEPTH;
      R = clamp(R / 1024, 1, new_depth);
      current_score = -negamax(pos, thread, ss + 1, -alpha - 1, -alpha,
                               new_depth - R + 1, 1);

      needs_full_search = current_score > alpha && R > 0;
    } else {
      needs_full_search = !pv_node || legal_moves > 1;
    }

    if (needs_full_search) {
      current_score = -negamax(pos, thread, ss + 1, -alpha - 1, -alpha,
                               new_depth, !cutnode);
    }

    if (pv_node && (legal_moves == 1 || current_score > alpha)) {
      current_score =
          -negamax(pos, thread, ss + 1, -beta, -alpha, new_depth, 0);
    }

    // decrement ply
    pos->ply--;

    // decrement repetition index
    pos->repetition_index--;

    // take move back
    restore_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
                  pos->castle, pos->fifty, pos->hash_key, pos->mailbox);

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
            thread->killer_moves[pos->ply] = move;
          }

          update_capture_history_moves(thread, capture_list, best_move, depth);
          break;
        }
      }
    }
  }

  // we don't have any legal moves to make in the current postion
  if (legal_moves == 0) {
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
    write_hash_entry(pos, best_score, depth, best_move, hash_flag);
  }

  // node (position) fails low
  return best_score;
}

static void print_thinking(thread_t *thread, int score, int current_depth) {

  uint64_t nodes = total_nodes(thread, thread_count);
  uint64_t time = get_time_ms() - thread->starttime;
  uint64_t nps = (nodes / fmax(time, 1)) * 1000;

  printf("info depth %d seldepth %d score ", current_depth,
         thread->pos.seldepth);

  if (score > -MATE_VALUE && score < -MATE_SCORE) {
    printf("mate %d ", -(score + MATE_VALUE) / 2 - 1);
  } else if (score > MATE_SCORE && score < MATE_VALUE) {
    printf("mate %d ", (MATE_VALUE - score) / 2 + 1);
  } else {
    printf("cp %d ", 100 * score / 212);
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
    int alpha = -INF;
    int beta = INF;

    searchstack_t ss[MAX_PLY + 4];
    for (int i = 0; i < MAX_PLY + 4; ++i) {
      ss[i].excluded_move = 0;
      ss[i].static_eval = NO_SCORE;
      ss[i].history_score = 0;
      ss[i].move = 0;
      ss[i].piece = 0;
      ss[i].null_move = 0;
    }

    pos->seldepth = 0;

    int window = ASP_WINDOW;

    int fail_high_count = 0;

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
                              thread->depth - fail_high_count, 0);

      // We hit an apspiration window cut-off before time ran out and we jumped
      // to another depth with wider search which we didnt finish
      if (thread->stopped) {
        return NULL;
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
        scale_time(thread, best_move_stability, eval_stability);
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
  }

  // clear helper data structures for search
  memset(threads->pv.pv_table, 0, sizeof(threads->pv.pv_table));
  memset(threads->pv.pv_length, 0, sizeof(threads->pv.pv_length));

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
