#include "movepicker.h"
#include "attacks.h"
#include "history.h"
#include "move.h"
#include "movegen.h"
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

static inline void score_noisy(move_picker_t *picker, move_t *move_entry) {
  uint16_t move = move_entry->move;
  uint8_t source = get_move_source(move);
  uint8_t target = get_move_target(move);

  uint8_t source_threatened = is_square_threatened(picker->ss, source);
  uint8_t target_threatened = is_square_threatened(picker->ss, target);

  // Determine target piece (handle en passant)
  int target_piece;
  if (get_move_enpassant(move)) {
    int ep_square = picker->pos->side ? target - 8 : target + 8;
    target_piece = picker->pos->mailbox[ep_square];
  } else {
    target_piece = picker->pos->mailbox[target];
  }

  // MVV-LVA base score
  move_entry->score = mvv[target_piece % 6] * MO_MVV_MULT;

  // Add capture history
  move_entry->score +=
      picker->thread
          ->capture_history[picker->pos->mailbox[source]][target_piece][source]
                           [target][source_threatened][target_threatened] *
      MO_CAPT_HIST_MULT;

  move_entry->score /= 1024;

  // SEE check - good captures get huge bonus, bad ones get penalty
  int see_threshold = -MO_SEE_THRESHOLD - move_entry->score / 35;
  move_entry->score +=
      SEE(picker->pos, move, see_threshold) ? 1000000000 : -1000000000;
}

static inline void score_quiet(move_picker_t *picker, move_t *move_entry) {
  uint16_t move = move_entry->move;

  move_entry->score =
      picker->thread->quiet_history
              [picker->pos->side][get_move_source(move)][get_move_target(move)]
              [is_square_threatened(picker->ss, get_move_source(move))]
              [is_square_threatened(picker->ss, get_move_target(move))] *
          MO_QUIET_HIST_MULT +
      get_conthist_score(picker->thread, picker->pos, picker->ss, move, 1) *
          MO_CONT1_HIST_MULT +
      get_conthist_score(picker->thread, picker->pos, picker->ss, move, 2) *
          MO_CONT2_HIST_MULT +
      get_conthist_score(picker->thread, picker->pos, picker->ss, move, 4) *
          MO_CONT4_HIST_MULT +
      picker->thread->pawn_history[picker->pos->hash_keys.pawn_key % 2048]
                                  [picker->pos->mailbox[get_move_source(move)]]
                                  [get_move_target(move)] *
          MO_PAWN_HIST_MULT;
  move_entry->score /= 1024;
}

static inline void score_move(move_picker_t *picker, move_t *move_entry) {
  uint16_t move = move_entry->move;

  // Cache frequently used values
  uint8_t source = get_move_source(move);
  uint8_t target = get_move_target(move);
  uint8_t promoted_piece = get_move_promoted(picker->pos->side, move);
  uint8_t is_capture = get_move_capture(move);

  uint8_t source_threatened = is_square_threatened(picker->ss, source);
  uint8_t target_threatened = is_square_threatened(picker->ss, target);

  // Handle noisy moves
  if (is_capture || promoted_piece) {
    // Determine target piece (handle en passant)
    int target_piece;
    if (get_move_enpassant(move)) {
      int ep_square = picker->pos->side ? target - 8 : target + 8;
      target_piece = picker->pos->mailbox[ep_square];
    } else {
      target_piece = picker->pos->mailbox[target];
    }

    // MVV-LVA base score
    move_entry->score = mvv[target_piece % 6] * MO_MVV_MULT;

    // Add capture history
    move_entry->score +=
        picker->thread->capture_history[picker->pos->mailbox[source]]
                                       [target_piece][source][target]
                                       [source_threatened][target_threatened] *
        MO_CAPT_HIST_MULT;

    move_entry->score /= 1024;

    // SEE check - good captures get huge bonus, bad ones get penalty
    int see_threshold = -MO_SEE_THRESHOLD - move_entry->score / 35;
    move_entry->score +=
        SEE(picker->pos, move, see_threshold) ? 1000000000 : -1000000000;
    return;
  }

  // Handle quiet moves
  move_entry->score =
      picker->thread->quiet_history[picker->pos->side][source][target]
                                   [source_threatened][target_threatened] *
          MO_QUIET_HIST_MULT +
      get_conthist_score(picker->thread, picker->pos, picker->ss, move, 1) *
          MO_CONT1_HIST_MULT +
      get_conthist_score(picker->thread, picker->pos, picker->ss, move, 2) *
          MO_CONT2_HIST_MULT +
      get_conthist_score(picker->thread, picker->pos, picker->ss, move, 4) *
          MO_CONT4_HIST_MULT +
      picker->thread->pawn_history[picker->pos->hash_keys.pawn_key % 2048]
                                  [picker->pos->mailbox[source]][target] *
          MO_PAWN_HIST_MULT;

  move_entry->score /= 1024;
}

static inline uint16_t pick_best(moves *list, uint16_t *index) {
  if (*index >= list->count)
    return 0;

  uint16_t best = *index;
  for (uint16_t i = *index + 1; i < list->count; ++i) {
    if (list->entry[i].score > list->entry[best].score)
      best = i;
  }

  if (best != *index) {
    move_t temp = list->entry[*index];
    list->entry[*index] = list->entry[best];
    list->entry[best] = temp;
  }

  return list->entry[(*index)++].move;
}

void init_picker(move_picker_t *picker, position_t *pos, thread_t *thread,
                 searchstack_t *ss, uint16_t tt_move) {
  picker->pos = pos;
  picker->thread = thread;
  picker->ss = ss;
  picker->tt_move = tt_move;
  picker->stage = STAGE_TT_MOVE;
  picker->type = PICKER_MAIN_SEARCH;
  picker->noisy_index = 0;
  picker->quiet_index = 0;
  picker->bad_noisy_index = 0;
  picker->skip_quiets = 0;
  picker->noisy_moves->count = 0;
  picker->quiet_moves->count = 0;
  picker->bad_noisy->count = 0;
}

void init_qsearch_picker(move_picker_t *picker, position_t *pos,
                         thread_t *thread, searchstack_t *ss,
                         uint16_t tt_move) {
  picker->pos = pos;
  picker->thread = thread;
  picker->ss = ss;
  picker->tt_move = tt_move;
  picker->stage = STAGE_TT_MOVE;
  picker->type = PICKER_QSEARCH;
  picker->noisy_index = 0;
  picker->quiet_index = 0;
  picker->bad_noisy_index = 0;
  picker->skip_quiets = 0;
  picker->noisy_moves->count = 0;
  picker->quiet_moves->count = 0;
  picker->bad_noisy->count = 0;
  picker->probcut = 0;
}

void init_probcut_picker(move_picker_t *picker, position_t *pos,
                         thread_t *thread, searchstack_t *ss) {
  picker->pos = pos;
  picker->thread = thread;
  picker->ss = ss;
  picker->tt_move = 0;
  picker->stage = STAGE_TT_MOVE;
  picker->type = PICKER_QSEARCH;
  picker->noisy_index = 0;
  picker->quiet_index = 0;
  picker->bad_noisy_index = 0;
  picker->skip_quiets = 0;
  picker->noisy_moves->count = 0;
  picker->quiet_moves->count = 0;
  picker->bad_noisy->count = 0;
  picker->probcut = 1;
}

uint16_t next_move(move_picker_t *picker, uint8_t skip_quiets) {
  picker->skip_quiets = skip_quiets;

  while (1) {
    switch (picker->stage) {
    case STAGE_TT_MOVE:
      picker->stage = STAGE_GENERATE_MOVES;
      if (picker->tt_move && is_pseudo_legal(picker->pos, picker->tt_move)) {
        return picker->tt_move;
      }
      continue;

    case STAGE_GENERATE_MOVES: {
      if (picker->type == PICKER_MAIN_SEARCH ||
          (picker->type == PICKER_QSEARCH && stm_in_check(picker->pos) &&
           picker->probcut == 0)) {
        generate_moves(picker->pos, picker->quiet_moves);
      } else {
        generate_noisy(picker->pos, picker->quiet_moves);
      }

      for (uint32_t i = 0; i < picker->quiet_moves->count; i++) {
        score_move(picker, &picker->quiet_moves->entry[i]);
      }
      picker->stage = STAGE_PLAY_MOVES;
      continue;
    }

    case STAGE_PLAY_MOVES: {
      while (picker->quiet_index < picker->quiet_moves->count) {
        uint16_t move = pick_best(picker->quiet_moves, &picker->quiet_index);

        if (move == picker->tt_move)
          continue;

        return move;
      }

      picker->stage = STAGE_DONE;
      continue;
    }

      /*case STAGE_GENERATE_NOISY:
        generate_noisy(picker->pos, picker->noisy_moves);
        picker->stage = STAGE_GOOD_NOISY;

        // Score all noisy moves
        for (uint32_t i = 0; i < picker->noisy_moves->count; i++) {
          score_noisy(picker, &picker->noisy_moves->entry[i]);
        }
        continue;

      case STAGE_GOOD_NOISY: {
        while (picker->noisy_index < picker->noisy_moves->count) {
          uint16_t move = pick_best(picker->noisy_moves, &picker->noisy_index);

          if (move == picker->tt_move)
            continue;

          // SEE pruning for good captures
          if (SEE(picker->pos, move, -MO_SEE_THRESHOLD)) {
            return move;
          } else {
            // Save bad captures for later
            add_move(picker->bad_noisy, move);
          }
        }

        // Move to next stage
        if (picker->type == PICKER_QSEARCH) {
          if (stm_in_check(picker->pos) && picker->probcut == 0) {
            // In check, we already generated all moves
            picker->stage = STAGE_GENERATE_QUIET;
          } else {
            picker->stage = STAGE_BAD_NOISY;
          }
        } else {
          picker->stage = STAGE_GENERATE_QUIET;
        }
        continue;
      }

      case STAGE_GENERATE_QUIET:
        if (picker->skip_quiets) {
          picker->stage = STAGE_BAD_NOISY;
          continue;
        }

        generate_quiets(picker->pos, picker->quiet_moves);

        // Score all quiet moves
        for (uint32_t i = 0; i < picker->quiet_moves->count; i++) {
          score_quiet(picker, &picker->quiet_moves->entry[i]);
        }

        picker->stage = STAGE_QUIET;
        continue;

      case STAGE_QUIET: {
        if (picker->skip_quiets) {
          picker->stage = STAGE_BAD_NOISY;
          continue;
        }

        while (picker->quiet_index < picker->quiet_moves->count) {
          uint16_t move = pick_best(picker->quiet_moves, &picker->quiet_index);

          if (move == picker->tt_move)
            continue;

          return move;
        }

        picker->stage = STAGE_BAD_NOISY;
        continue;
      }

      case STAGE_BAD_NOISY:
        if (picker->bad_noisy_index < picker->bad_noisy->count) {
          return picker->bad_noisy->entry[picker->bad_noisy_index++].move;
        }
        picker->stage = STAGE_DONE;
        continue;*/

    case STAGE_DONE:
      return 0;
    }
  }
}