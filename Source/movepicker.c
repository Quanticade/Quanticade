#include "movepicker.h"
#include "attacks.h"
#include "enums.h"
#include "history.h"
#include "see.h"

extern int mvv[];
extern int MO_SEE_THRESHOLD;
extern int MO_QUIET_HIST_MULT;
extern int MO_CONT1_HIST_MULT;
extern int MO_CONT2_HIST_MULT;
extern int MO_CONT4_HIST_MULT;
extern int MO_PAWN_HIST_MULT;
extern int MO_CAPT_HIST_MULT;
extern int MO_MVV_MULT;

void init_picker(move_picker_t *picker, moves *move_list) {
  picker->move_list = move_list;
  picker->index = 0;
}

void score_moves(position_t *pos, thread_t *thread, searchstack_t *ss,
                 moves *move_list, uint16_t tt_move) {
  for (uint32_t i = 0; i < move_list->count; i++) {
    uint16_t move = move_list->entry[i].move;

    // TT move gets highest priority
    if (move == tt_move) {
      move_list->entry[i].score = 2000000000;
      continue;
    }

    // Cache frequently used values
    uint8_t source = get_move_source(move);
    uint8_t target = get_move_target(move);
    uint8_t promoted_piece = get_move_promoted(pos->side, move);
    uint8_t is_capture = get_move_capture(move);

    uint8_t source_threatened = is_square_threatened(ss, source);
    uint8_t target_threatened = is_square_threatened(ss, target);

    // Handle noisy moves
    if (is_capture || promoted_piece) {
      // Determine target piece (handle en passant)
      int target_piece;
      if (get_move_enpassant(move)) {
        int ep_square = pos->side ? target - 8 : target + 8;
        target_piece = pos->mailbox[ep_square];
      } else {
        target_piece = pos->mailbox[target];
      }

      // MVV-LVA base score
      move_list->entry[i].score = mvv[target_piece % 6] * MO_MVV_MULT;

      // Add capture history
      move_list->entry[i].score +=
          thread
              ->capture_history[pos->mailbox[source]][target_piece][source]
                               [target][source_threatened][target_threatened] *
          MO_CAPT_HIST_MULT;

      move_list->entry[i].score /= 1024;

      // SEE check - good captures get huge bonus, bad ones get penalty
      int see_threshold = -MO_SEE_THRESHOLD - move_list->entry[i].score / 35;
      move_list->entry[i].score +=
          SEE(pos, move, see_threshold) ? 1000000000 : -1000000000;
      return;
    }

    // Handle quiet moves
    move_list->entry[i].score =
        thread->quiet_history[pos->side][source][target][source_threatened]
                             [target_threatened] *
            MO_QUIET_HIST_MULT +
        get_conthist_score(thread, pos, ss, move, 1) * MO_CONT1_HIST_MULT +
        get_conthist_score(thread, pos, ss, move, 2) * MO_CONT2_HIST_MULT +
        get_conthist_score(thread, pos, ss, move, 4) * MO_CONT4_HIST_MULT +
        thread->pawn_history[pos->hash_keys.pawn_key % 2048]
                            [pos->mailbox[source]][target] *
            MO_PAWN_HIST_MULT;

    move_list->entry[i].score /= 1024;
  }
}

uint16_t next_move(move_picker_t *picker) {
  if (picker->index >= picker->move_list->count)
    return 0;

  // Find best move in remaining moves
  uint16_t best = picker->index;
  for (uint16_t i = picker->index + 1; i < picker->move_list->count; ++i) {
    if (picker->move_list->entry[i].score >
        picker->move_list->entry[best].score) {
      best = i;
    }
  }

  // Swap best to current position
  if (best != picker->index) {
    move_t temp = picker->move_list->entry[picker->index];
    picker->move_list->entry[picker->index] = picker->move_list->entry[best];
    picker->move_list->entry[best] = temp;
  }

  return picker->move_list->entry[picker->index++].move;
}