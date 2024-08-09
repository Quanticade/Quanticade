#include "search.h"
#include "attacks.h"
#include "bitboards.h"
#include "enums.h"
#include "evaluate.h"
#include "movegen.h"
#include "nnue.h"
#include "pvtable.h"
#include "pyrrhic/tbprobe.h"
#include "structs.h"
#include "syzygy.h"
#include "threads.h"
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

int LMP_BASE = 6;
int LMP_MULTIPLIER = 1;
int RAZOR_DEPTH = 7;
int RAZOR_MARGIN = 295;
int RFP_DEPTH = 6;
int RFP_MARGIN = 102;
int NMP_BASE_REDUCTION = 7;
int NMP_DIVISER = 8;
int NMP_DEPTH = 1;
int IIR_DEPTH = 4;
int SEE_QUIET = 66;
int SEE_CAPTURE = 32;
int SEE_DEPTH = 10;
int QS_SEE_THRESHOLD = 7;
int MO_SEE_THRESHOLD = 108;

const int full_depth_moves = 4;
const int reduction_limit = 3;

const int mvv_lva[12][12] = {
    {105, 205, 305, 405, 505, 605, 105, 205, 305, 405, 505, 605},
    {104, 204, 304, 404, 504, 604, 104, 204, 304, 404, 504, 604},
    {103, 203, 303, 403, 503, 603, 103, 203, 303, 403, 503, 603},
    {102, 202, 302, 402, 502, 602, 102, 202, 302, 402, 502, 602},
    {101, 201, 301, 401, 501, 601, 101, 201, 301, 401, 501, 601},
    {100, 200, 300, 400, 500, 600, 100, 200, 300, 400, 500, 600},

    {105, 205, 305, 405, 505, 605, 105, 205, 305, 405, 505, 605},
    {104, 204, 304, 404, 504, 604, 104, 204, 304, 404, 504, 604},
    {103, 203, 303, 403, 503, 603, 103, 203, 303, 403, 503, 603},
    {102, 202, 302, 402, 502, 602, 102, 202, 302, 402, 502, 602},
    {101, 201, 301, 401, 501, 601, 101, 201, 301, 401, 501, 601},
    {100, 200, 300, 400, 500, 600, 100, 200, 300, 400, 500, 600}};

int SEEPieceValues[] = {100, 300, 295, 509, 1203, 0, 0};

/*  =======================
         Move ordering
    =======================

    1. PV move
    2. Captures in MVV/LVA
    3. 1st killer move
    4. 2nd killer move
    5. History moves
    6. Unsorted moves
*/

int reductions[32][32];

// Initializes the late move reduction array
static void init_reductions(void) __attribute__((constructor));
static void init_reductions(void) {

  for (int depth = 0; depth < 32; ++depth) {
    for (int moves = 0; moves < 32; ++moves) {
      if (depth == 0 || moves == 0) {
        reductions[depth][moves] = 0;
      } else {
        reductions[depth][moves] = 0.75 + log(depth) * log(moves) / 2.25;
      }
    }
  }
}

void check_time(thread_t *thread) {
  // if time is up break here
  if (thread->timeset == 1 && get_time_ms() > thread->stoptime) {
    // tell engine to stop calculating
    thread->stopped = 1;
  }
}

// enable PV move scoring
static inline void enable_pv_scoring(position_t *pos, thread_t *thread,
                                     moves *move_list) {
  // disable following PV
  thread->pv.follow_pv = 0;

  // loop over the moves within a move list
  for (uint32_t count = 0; count < move_list->count; count++) {
    // make sure we hit PV move
    if (thread->pv.pv_table[0][pos->ply] == move_list->entry[count].move) {
      // enable move scoring
      thread->pv.score_pv = 1;

      // enable following PV
      thread->pv.follow_pv = 1;
    }
  }
}

int move_estimated_value(position_t *pos, int move) {

  // Start with the value of the piece on the target square
  int target_piece = pos->mailbox[get_move_target(move)] > 5
                         ? pos->mailbox[get_move_target(move)] - 6
                         : pos->mailbox[get_move_target(move)];
  int promoted_piece = get_move_promoted(move) > 5 ? get_move_promoted(move) - 6
                                                   : get_move_promoted(move);
  int value = SEEPieceValues[target_piece];

  // Factor in the new piece's value and remove our promoted pawn
  if (get_move_promoted(move))
    value += SEEPieceValues[promoted_piece] - SEEPieceValues[PAWN];

  // Target square is encoded as empty for enpass moves
  else if (get_move_enpassant(move))
    value = SEEPieceValues[PAWN];

  // We encode Castle moves as KxR, so the initial step is wrong
  else if (get_move_castling(move))
    value = 0;

  return value;
}

uint64_t all_attackers_to_square(position_t *pos, uint64_t occupied, int sq) {

  // When performing a static exchange evaluation we need to find all
  // attacks to a given square, but we also are given an updated occupied
  // bitboard, which will likely not match the actual board, as pieces are
  // removed during the iterations in the static exchange evaluation

  return (get_pawn_attacks(white, sq) & pos->bitboards[p]) |
         (get_pawn_attacks(black, sq) & pos->bitboards[P]) |
         (get_knight_attacks(sq) & (pos->bitboards[n] | pos->bitboards[N])) |
         (get_bishop_attacks(sq, occupied) &
          ((pos->bitboards[b] | pos->bitboards[B]) |
           (pos->bitboards[q] | pos->bitboards[Q]))) |
         (get_rook_attacks(sq, occupied) &
          ((pos->bitboards[r] | pos->bitboards[R]) |
           (pos->bitboards[q] | pos->bitboards[Q]))) |
         (get_king_attacks(sq) & (pos->bitboards[k] | pos->bitboards[K]));
}

int SEE(position_t *pos, int move, int threshold) {

  int from, to, enpassant, promotion, colour, balance, nextVictim;
  uint64_t bishops, rooks, occupied, attackers, myAttackers;

  // Unpack move information
  from = get_move_source(move);
  to = get_move_target(move);
  enpassant = get_move_enpassant(move);
  promotion = get_move_promoted(move);

  // Next victim is moved piece or promotion type
  nextVictim = promotion ? promotion : pos->mailbox[from];
  nextVictim = nextVictim > 5 ? nextVictim - 6 : nextVictim;

  // Balance is the value of the move minus threshold. Function
  // call takes care for Enpass, Promotion and Castling moves.
  balance = move_estimated_value(pos, move) - threshold;

  // Best case still fails to beat the threshold
  if (balance < 0)
    return 0;

  // Worst case is losing the moved piece
  balance -= SEEPieceValues[nextVictim];

  // If the balance is positive even if losing the moved piece,
  // the exchange is guaranteed to beat the threshold.
  if (balance >= 0)
    return 1;

  // Grab sliders for updating revealed attackers
  bishops = pos->bitboards[b] | pos->bitboards[B] | pos->bitboards[q] |
            pos->bitboards[Q];
  rooks = pos->bitboards[r] | pos->bitboards[R] | pos->bitboards[q] |
          pos->bitboards[Q];

  // Let occupied suppose that the move was actually made
  occupied = pos->occupancies[both];
  occupied = (occupied ^ (1ull << from)) | (1ull << to);
  if (enpassant)
    occupied ^= (1ull << pos->enpassant);

  // Get all pieces which attack the target square. And with occupied
  // so that we do not let the same piece attack twice
  attackers = all_attackers_to_square(pos, occupied, to) & occupied;

  // Now our opponents turn to recapture
  colour = pos->side ^ 1;

  while (1) {

    // If we have no more attackers left we lose
    myAttackers = attackers & pos->occupancies[colour];
    if (myAttackers == 0ull) {
      // printf("WELL FUCK\n");
      break;
    }

    // Find our weakest piece to attack with
    for (nextVictim = PAWN; nextVictim <= QUEEN; nextVictim++) {
      if (myAttackers &
          (pos->bitboards[nextVictim] | pos->bitboards[nextVictim + 6])) {
        // printf("Taking with %d\n", nextVictim);
        break;
      }
    }

    // Remove this attacker from the occupied
    occupied ^=
        (1ull << get_lsb(myAttackers & (pos->bitboards[nextVictim] |
                                        pos->bitboards[nextVictim + 6])));

    // A diagonal move may reveal bishop or queen attackers
    if (nextVictim == PAWN || nextVictim == BISHOP || nextVictim == QUEEN)
      attackers |= get_bishop_attacks(to, occupied) & bishops;

    // A vertical or horizontal move may reveal rook or queen attackers
    if (nextVictim == ROOK || nextVictim == QUEEN)
      attackers |= get_rook_attacks(to, occupied) & rooks;

    // Make sure we did not add any already used attacks
    attackers &= occupied;

    // Swap the turn
    colour = !colour;

    // Negamax the balance and add the value of the next victim
    balance = -balance - 1 - SEEPieceValues[nextVictim];

    // If the balance is non negative after giving away our piece then we win
    if (balance >= 0) {

      // As a slide speed up for move legality checking, if our last attacking
      // piece is a king, and our opponent still has attackers, then we've
      // lost as the move we followed would be illegal
      if (nextVictim == KING && (attackers & pos->occupancies[colour]))
        colour = colour ^ 1;

      break;
    }
  }

  // Side to move after the loop loses
  return pos->side != colour;
}

// score moves
static inline void score_move(position_t *pos, thread_t *thread,
                              move_t *move_entry, int hash_move) {
  int move = move_entry->move;
  if (move == hash_move) {
    move_entry->score = 2000000000;
    return;
  }

  // if PV move scoring is allowed
  if (thread->pv.score_pv) {
    // make sure we are dealing with PV move
    if (thread->pv.pv_table[0][pos->ply] == move) {
      // disable score PV flag
      thread->pv.score_pv = 0;

      // give PV move the highest score to search it first
      move_entry->score = 1500000000;
      return;
    }
  }

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
    move_entry->score = mvv_lva[get_move_piece(move)][target_piece];
    move_entry->score += SEE(pos, move, -MO_SEE_THRESHOLD) ? 1000000000 : -1000000;
    return;
  }

  // score quiet move
  else {
    // score 1st killer move
    if (thread->killer_moves[0][pos->ply] == move) {
      move_entry->score = 900000000;
    }

    // score 2nd killer move
    else if (thread->killer_moves[1][pos->ply] == move) {
      move_entry->score = 800000000;
    }

    // score history move
    else {
      move_entry->score =
          thread->history_moves[get_move_piece(move)][get_move_target(move)];
    }

    return;
  }

  move_entry->score = 0;
  return;
}

// sort moves in descending order
static inline void sort_moves(moves *move_list) {
  // loop over current move within a move list
  for (uint32_t current_move = 0; current_move < move_list->count;
       current_move++) {
    // loop over next move within a move list
    for (uint32_t next_move = current_move + 1; next_move < move_list->count;
         next_move++) {
      // compare current and next move scores
      if (move_list->entry[current_move].score <
          move_list->entry[next_move].score) {
        // swap scores
        int temp_score = move_list->entry[current_move].score;
        move_list->entry[current_move].score =
            move_list->entry[next_move].score;
        move_list->entry[next_move].score = temp_score;

        // swap moves
        int temp_move = move_list->entry[current_move].move;
        move_list->entry[current_move].move = move_list->entry[next_move].move;
        move_list->entry[next_move].move = temp_move;
      }
    }
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

// quiescence search
static inline int quiescence(position_t *pos, thread_t *thread, int alpha,
                             int beta) {
  // Check on time
  check_time(thread);

  // we are too deep, hence there's an overflow of arrays relying on max ply
  // constant
  if (pos->ply > max_ply - 1)
    // evaluate position
    return evaluate(pos);

  int32_t best_move = 0;
  int score = 0;
  int pv_node = beta - alpha > 1;
  int hash_flag = hash_flag_alpha;

  if (pos->ply &&
      (score = read_hash_entry(pos, alpha, &best_move, beta, 0)) !=
          no_hash_entry &&
      pv_node == 0) {
    // if the move has already been searched (hence has a value)
    // we just return the score for this move without searching it
    return score;
  }

  // evaluate position
  score = evaluate(pos);

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

  // generate moves
  generate_captures(pos, move_list);

  for (uint32_t count = 0; count < move_list->count; count++) {
    score_move(pos, thread, &move_list->entry[count], best_move);
  }

  sort_moves(move_list);

  int best_score = score;
  score = -infinity;

  // loop over moves within a movelist
  for (uint32_t count = 0; count < move_list->count; count++) {

    if (!SEE(pos, move_list->entry[count].move, -QS_SEE_THRESHOLD))
      continue;

    // preserve board state
    copy_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
               pos->castle, pos->fifty, pos->hash_key, pos->mailbox,
               pos->accumulator.accumulator);

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
    accumulator_make_move(pos, move_list->entry[count].move, mailbox_copy);

    thread->nodes++;

    // score current move
    score = -quiescence(pos, thread, -beta, -alpha);

    // decrement ply
    pos->ply--;

    // decrement repetition index
    pos->repetition_index--;

    // take move back
    restore_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
                  pos->castle, pos->fifty, pos->hash_key, pos->mailbox,
                  pos->accumulator.accumulator);

    // return 0 if time is up
    if (thread->stopped == 1) {
      return 0;
    }

    if (score > best_score) {
      best_score = score;
    }

    // found a better move
    if (score > alpha) {
      // fail-hard beta cutoff
      hash_flag = hash_flag_exact;
      best_move = move_list->entry[count].move;
      if (score >= beta) {
        write_hash_entry(pos, best_score, 0, best_move, hash_flag_beta);
        // node (position) fails high
        return best_score;
      }
      // PV node (position)
      alpha = score;
    }
  }
  write_hash_entry(pos, best_score, 0, best_move, hash_flag);
  // node (position) fails low
  return best_score;
}

double clamp(double d, double min, double max) {
  const double t = d < min ? min : d;
  return t > max ? max : t;
}

static inline void update_history_move(thread_t *thread, int move,
                                       uint8_t depth, uint8_t is_best_move) {
  int piece = get_move_piece(move);
  int target = get_move_target(move);
  int bonus = 16 * depth * depth + 32 * depth + 16;
  int clamped_bonus = clamp(is_best_move ? bonus : -bonus, -1200, 1200);
  thread->history_moves[piece][target] +=
      clamped_bonus -
      thread->history_moves[piece][target] * abs(clamped_bonus) / 8192;
}

static inline void update_all_history_moves(thread_t *thread,
                                            moves *quiet_moves, int best_move,
                                            uint8_t depth) {
  for (uint32_t i = 0; i < quiet_moves->count; ++i) {
    if (quiet_moves->entry[i].move == best_move) {
      update_history_move(thread, best_move, depth, 1);
    } else {
      update_history_move(thread, quiet_moves->entry[i].move, depth, 0);
    }
  }
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

// negamax alpha beta search
static inline int negamax(position_t *pos, thread_t *thread, int alpha,
                          int beta, int depth, uint8_t do_null_pruning) {
  // init PV length
  thread->pv.pv_length[pos->ply] = pos->ply;

  // variable to store current move's score (from the static evaluation
  // perspective)
  int score = -infinity;

  int tt_move = 0;

  uint8_t root_node = pos->ply == 0;

  // define hash flag
  int hash_flag = hash_flag_alpha;

  if (!root_node) {
    // if position repetition occurs
    if (is_repetition(pos) || pos->fifty >= 100 || is_material_draw(pos)) {
      // return draw score
      return 0;
    }

    // we are too deep, hence there's an overflow of arrays relying on max ply
    // constant
    if (pos->ply > max_ply - 1) {
      // evaluate position
      return evaluate(pos);
    }

    // Mate distance pruning
    alpha = MAX(alpha, -mate_value + (int)pos->ply);
    beta = MIN(beta, mate_value - (int)pos->ply - 1);
    if (alpha >= beta)
      return alpha;
  }

  // a hack by Pedro Castro to figure out whether the current node is PV node
  // or not
  int pv_node = beta - alpha > 1;

  // read hash entry if we're not in a root ply and hash entry is available
  // and current node is not a PV node
  if (!root_node &&
      (score = read_hash_entry(pos, alpha, &tt_move, beta, depth)) !=
          no_hash_entry &&
      pv_node == 0) {
    // if the move has already been searched (hence has a value)
    // we just return the score for this move without searching it
    return score;
  }

  // Check on time
  check_time(thread);

  int r;

  // is king in check
  int in_check = is_square_attacked(pos,
                                    (pos->side == white)
                                        ? __builtin_ctzll(pos->bitboards[K])
                                        : __builtin_ctzll(pos->bitboards[k]),
                                    pos->side ^ 1);

  // increase search depth if the king has been exposed into a check
  if (in_check) {
    depth++;
  }

  // recursion escape condition
  if (depth == 0) {
    // run quiescence search
    return quiescence(pos, thread, alpha, beta);
  }

  // legal moves counter
  int legal_moves = 0;

  int static_eval = evaluate(pos);
  if (!in_check) {
    // Reverse Futility Pruning
    if (depth <= RFP_DEPTH && !pv_node && abs(beta - 1) > -infinity + 100) {
      // get static evaluation score

      // define evaluation margin
      int eval_margin = RFP_MARGIN * depth;

      // evaluation margin substracted from static evaluation score fails high
      if (static_eval - eval_margin >= beta)
        // evaluation margin substracted from static evaluation score
        return static_eval - eval_margin;
    }

    // null move pruning
    if (do_null_pruning && depth >= NMP_DEPTH && !root_node &&
        static_eval >= beta) {
      int R = NMP_BASE_REDUCTION + depth / NMP_DIVISER;
      R = MIN(R, depth);
      // preserve board state
      copy_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
                 pos->castle, pos->fifty, pos->hash_key, pos->mailbox,
                 pos->accumulator.accumulator);

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

      /* search moves with reduced depth to find beta cutoffs
         depth - 1 - R where R is a reduction limit */
      score = -negamax(pos, thread, -beta, -beta + 1, depth - R, 0);

      // decrement ply
      pos->ply--;

      // decrement repetition index
      pos->repetition_index--;

      // restore board state
      restore_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
                    pos->castle, pos->fifty, pos->hash_key, pos->mailbox,
                    pos->accumulator.accumulator);

      // return 0 if time is up
      if (thread->stopped == 1) {
        return 0;
      }

      // fail-hard beta cutoff
      if (score >= beta)
        // node (position) fails high
        return score;
    }

    if (!pv_node && depth <= RAZOR_DEPTH &&
        static_eval + RAZOR_MARGIN * depth < alpha) {
      const int razor_score = quiescence(pos, thread, alpha, beta);
      if (razor_score <= alpha) {
        return razor_score;
      }
    }

    // Internal Iterative Reductions
    if (depth >= IIR_DEPTH && !tt_move) {
      depth--;
    }
  }

  // create move list instance
  moves move_list[1];
  moves quiet_list[1];
  quiet_list->count = 0;

  // generate moves
  generate_moves(pos, move_list);

  int best_score = -infinity;
  score = -infinity;

  // if we are now following PV line
  if (thread->pv.follow_pv)
    // enable PV move scoring
    enable_pv_scoring(pos, thread, move_list);

  int best_move = 0;
  for (uint32_t count = 0; count < move_list->count; count++) {
    score_move(pos, thread, &move_list->entry[count], tt_move);
  }

  sort_moves(move_list);

  // number of moves searched in a move list
  int moves_searched = 0;

  uint8_t skip_quiets = 0;

  // loop over moves within a movelist
  for (uint32_t count = 0; count < move_list->count; count++) {
    int list_move = move_list->entry[count].move;
    uint8_t quiet = (get_move_capture(list_move) == 0);

    if (skip_quiets && quiet) {
      continue;
    }

    // Late Move Pruning
    if (!pv_node && !in_check && quiet &&
        legal_moves > LMP_BASE + LMP_MULTIPLIER * depth * depth) {
      skip_quiets = 1;
    }

    const int see_threshold = quiet ? -SEE_QUIET * depth : -SEE_CAPTURE * depth * depth;
    if (depth <= SEE_DEPTH && legal_moves > 0 && !SEE(pos, list_move, see_threshold))
      continue;

    int move = move_list->entry[count].move;
    uint8_t is_quiet = get_move_capture(move) == 0;

    // preserve board state
    copy_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
               pos->castle, pos->fifty, pos->hash_key, pos->mailbox,
               pos->accumulator.accumulator);

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
    accumulator_make_move(pos, move, mailbox_copy);

    // increment nodes count
    thread->nodes++;

    // increment legal moves
    legal_moves++;
    // increment the counter of moves searched so far
    moves_searched++;

    if (is_quiet) {
      add_move(quiet_list, move);
    }

    uint8_t move_is_noisy =
        in_check == 0 && is_quiet && get_move_promoted(move) == 0;
    uint8_t do_lmr = depth > 2 && moves_searched > (2 + pv_node) && pos->ply &&
                     move_is_noisy;

    // condition to consider LMR
    if (do_lmr) {

      r = reductions[MIN(31, depth)][MIN(31, moves_searched)];
      r += !pv_node;

      int reddepth = MAX(1, depth - 1 - MAX(r, 1));
      // search current move with reduced depth:
      score = -negamax(pos, thread, -alpha - 1, -alpha, reddepth, 1);
    }

    if ((do_lmr && score > alpha) ||
        (!do_lmr && (!pv_node || moves_searched > 1))) {
      score = -negamax(pos, thread, -alpha - 1, -alpha, depth - 1, 1);
    }

    if (pv_node && ((score > alpha && score < beta) || moves_searched == 1)) {
      score = -negamax(pos, thread, -beta, -alpha, depth - 1, 1);
    }

    // decrement ply
    pos->ply--;

    // decrement repetition index
    pos->repetition_index--;

    // take move back
    restore_board(pos->bitboards, pos->occupancies, pos->side, pos->enpassant,
                  pos->castle, pos->fifty, pos->hash_key, pos->mailbox,
                  pos->accumulator.accumulator);

    // return infinity so we can deal with timeout in case we are doing
    // re-search
    if (thread->stopped == 1) {
      return infinity;
    }

    // found a better move
    if (score > best_score) {
      best_score = score;
      if (score > alpha) {
        // switch hash flag from storing score for fail-low node
        // to the one storing score for PV node
        hash_flag = hash_flag_exact;

        best_move = move;

        // PV node (position)
        alpha = score;

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
        if (score >= beta) {
          // store hash entry with the score equal to beta
          write_hash_entry(pos, best_score, depth, best_move, hash_flag_beta);

          // on quiet moves
          if (is_quiet) {
            update_all_history_moves(thread, quiet_list, best_move, depth);
            // store killer moves
            thread->killer_moves[1][pos->ply] =
                thread->killer_moves[0][pos->ply];
            thread->killer_moves[0][pos->ply] = move;
          }

          // node (position) fails high
          return best_score;
        }
      }
    }
  }

  // we don't have any legal moves to make in the current postion
  if (legal_moves == 0) {
    // king is in check
    if (in_check)
      // return mating score (assuming closest distance to mating position)
      return -mate_value + pos->ply;

    // king is not in check
    else
      // return stalemate score
      return 0;
  }

  // store hash entry with the score equal to alpha
  write_hash_entry(pos, best_score, depth, best_move, hash_flag);

  // node (position) fails low
  return best_score;
}

static void print_thinking(thread_t *thread, int score, int current_depth) {

  uint64_t nodes = total_nodes(thread, thread_count);
  uint64_t time = get_time_ms() - thread->starttime;
  uint64_t nps = (nodes / fmax(time, 1)) * 1000;

  printf("info depth %d score ", current_depth);

  if (score > -mate_value && score < -mate_score) {
    printf("mate %d ", -(score + mate_value) / 2 - 1);
  } else if (score > mate_score && score < mate_value) {
    printf("mate %d ", (mate_value - score) / 2 + 1);
  } else {
    printf("cp %d ", score);
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

  int pv_table_copy[max_ply][max_ply];
  int pv_length_copy[max_ply];

  uint8_t window_ok = 1;

  // define initial alpha beta bounds
  int alpha = -infinity;
  int beta = infinity;

  // iterative deepening
  for (thread->depth = 1; thread->depth <= limits.depth; thread->depth++) {
    // if time is up
    if (thread->stopped == 1) {
      // stop calculating and return best move so far
      break;
    }

    // enable follow PV flag
    thread->pv.follow_pv = 1;

    // We should not save PV move from unfinished depth for example if depth
    // 12 finishes and goes to search depth 13 now but this triggers window
    // cutoff we dont want the info from depth 13 as its incomplete and in
    // case depth 14 search doesnt finish in time we will at least have an
    // full PV line from depth 12
    if (window_ok) {
      memcpy(pv_table_copy, thread->pv.pv_table, sizeof(thread->pv.pv_table));
      memcpy(pv_length_copy, thread->pv.pv_length,
             sizeof(thread->pv.pv_length));
    }

    // find best move within a given position
    thread->score = negamax(pos, thread, alpha, beta, thread->depth, 1);

    // Reset aspiration window OK flag back to 1
    window_ok = 1;

    // We hit an apspiration window cut-off before time ran out and we jumped
    // to another depth with wider search which we didnt finish
    if (thread->score == infinity) {
      // Restore the saved best line
      memcpy(thread->pv.pv_table, pv_table_copy, sizeof(pv_table_copy));
      memcpy(thread->pv.pv_length, pv_length_copy, sizeof(pv_length_copy));
      // Break out of the loop without printing info about the unfinished
      // depth
      break;
    }

    // we fell outside the window, so try again with a full-width window (and
    // the same depth)
    if ((thread->score <= alpha) || (thread->score >= beta)) {
      // Do a full window re-search
      alpha = -infinity;
      beta = infinity;
      window_ok = 0;
      thread->depth--;
      continue;
    }
    if (thread->index == 0) {
      // if PV is available
      if (thread->pv.pv_length[0]) {
        // print search info
        print_thinking(thread, thread->score, thread->depth);
      }
    }

    // set up the window for the next iteration
    alpha = thread->score - 50;
    beta = thread->score + 50;
  }
  return NULL;
}

// search position for the best move
void search_position(position_t *pos, thread_t *threads) {
  pthread_t pthreads[thread_count];
  for (int i = 0; i < thread_count; ++i) {
    threads[i].nodes = 0;
    threads[i].stopped = 0;
    threads[i].pv.follow_pv = 0;
    threads[i].pv.score_pv = 0;
    memset(threads[i].killer_moves, 0, sizeof(threads[i].killer_moves));
    memcpy(&threads[i].pos, pos, sizeof(position_t));
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
    threads[i].stopped = 1;
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
