#include "attacks.h"
#include "move.h"
#include "movegen.h"
#include "see.h"
#include "structs.h"
#include "history.h"

int MO_SEE_THRESHOLD = 119;
int MO_SEE_HISTORY_DIVISER = 33;
int MO_QUIET_HIST_MULT = 1087;
int MO_CONT1_HIST_MULT = 940;
int MO_CONT2_HIST_MULT = 940;
int MO_CONT4_HIST_MULT = 1075;
int MO_PAWN_HIST_MULT = 989;
int MO_CAPT_HIST_MULT = 1039;
int MO_MVV_MULT = 1011;

extern int mvv[];

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

// Scores noisy moves and splits them into good/bad lists based on SEE
static inline void score_noisy(position_t *pos, thread_t *thread,
                                searchstack_t *ss, moves *noisy_list,
                                moves *good_noisy, moves *bad_noisy,
                                uint16_t tt_move) {
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

    entry.score  = mvv[target_piece % 6] * MO_MVV_MULT;
    entry.score += thread->capture_history[pos->mailbox[source]][target_piece]
                                          [source][target]
                                          [source_threatened][target_threatened] *
                   MO_CAPT_HIST_MULT;
    entry.score /= 1024;

    int see_threshold = -MO_SEE_THRESHOLD - entry.score / MO_SEE_HISTORY_DIVISER;
    if (SEE(pos, move, see_threshold))
      good_noisy->entry[good_noisy->count++] = entry;
    else
      bad_noisy->entry[bad_noisy->count++] = entry;
  }
}

// Scores quiet moves in place
static inline void score_quiet(position_t *pos, thread_t *thread,
                                searchstack_t *ss, moves *quiet_list,
                                uint16_t tt_move) {
  for (uint32_t i = 0; i < quiet_list->count; i++) {
    move_t *entry = &quiet_list->entry[i];
    uint16_t move = entry->move;

    if (move == tt_move) {
      entry->score = INT32_MIN;
      continue;
    }

    uint8_t source = get_move_source(move);
    uint8_t target = get_move_target(move);
    uint8_t source_threatened = is_square_threatened(ss, source);
    uint8_t target_threatened = is_square_threatened(ss, target);

    entry->score =
        thread->quiet_history[pos->side][source][target]
                             [source_threatened][target_threatened] *
            MO_QUIET_HIST_MULT +
        get_conthist_score(thread, pos, ss, move, 1) * MO_CONT1_HIST_MULT +
        get_conthist_score(thread, pos, ss, move, 2) * MO_CONT2_HIST_MULT +
        get_conthist_score(thread, pos, ss, move, 4) * MO_CONT4_HIST_MULT +
        thread->pawn_history[pos->hash_keys.pawn_key % 2048]
                            [pos->mailbox[source]][target] *
            MO_PAWN_HIST_MULT;
    entry->score /= 1024;
  }
}

void init_picker(picker_t *picker, position_t *pos,
                                thread_t *thread, searchstack_t *ss,
                                uint16_t tt_move, uint8_t generate_all) {
  picker->stage             = STAGE_TABLE;
  picker->good_noisy.count  = 0;
  picker->bad_noisy.count   = 0;
  picker->quiets.count      = 0;
  picker->good_noisy_index  = 0;
  picker->bad_noisy_index   = 0;
  picker->quiet_index       = 0;
  picker->tt_move           = tt_move;
  picker->generate_all      = generate_all;
  picker->pos               = pos;
  picker->thread            = thread;
  picker->ss                = ss;
}

uint16_t select_next(picker_t *picker) {
  switch (picker->stage) {

  case STAGE_TABLE:
    picker->stage = STAGE_GENERATE_NOISY;
    if (picker->tt_move != 0
        && (picker->generate_all || get_move_capture(picker->tt_move) || is_move_promotion(picker->tt_move))
        && is_pseudo_legal(picker->pos, picker->tt_move)
        && is_legal(picker->pos, picker->tt_move))
      return picker->tt_move;
    /* fallthrough */

  case STAGE_GENERATE_NOISY: {
    moves tmp;
    generate_noisy(picker->pos, &tmp);
    score_noisy(picker->pos, picker->thread, picker->ss, &tmp,
                &picker->good_noisy, &picker->bad_noisy, picker->tt_move);
    picker->stage = STAGE_GOOD_NOISY;
    /* fallthrough */
  }

  case STAGE_GOOD_NOISY:
    while (picker->good_noisy_index < picker->good_noisy.count)
      return pick_next_best_move(&picker->good_noisy, &picker->good_noisy_index).move;
    if (!picker->generate_all) {
      picker->stage = STAGE_DONE;
      return 0;
    }
    picker->stage = STAGE_GENERATE_QUIET;
    /* fallthrough */

  case STAGE_GENERATE_QUIET:
    generate_quiets(picker->pos, &picker->quiets);
    score_quiet(picker->pos, picker->thread, picker->ss,
                &picker->quiets, picker->tt_move);
    picker->stage = STAGE_QUIET;
    /* fallthrough */

  case STAGE_QUIET:
    while (picker->quiet_index < picker->quiets.count) {
      uint16_t move = pick_next_best_move(&picker->quiets, &picker->quiet_index).move;
      if (move != picker->tt_move)
        return move;
    }
    picker->stage = STAGE_BAD_NOISY;
    /* fallthrough */

  case STAGE_BAD_NOISY:
    while (picker->bad_noisy_index < picker->bad_noisy.count)
      return pick_next_best_move(&picker->bad_noisy, &picker->bad_noisy_index).move;
    picker->stage = STAGE_DONE;
    /* fallthrough */

  case STAGE_DONE:
    return 0;

  default:
    return 0;
  }
}