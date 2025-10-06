#ifndef MOVEPICKER_H
#define MOVEPICKER_H

#include "move.h"
#include "structs.h"

typedef enum {
  STAGE_TT_MOVE,
  STAGE_GENERATE_NOISY,
  STAGE_GOOD_NOISY,
  STAGE_GENERATE_QUIET,
  STAGE_QUIET,
  STAGE_BAD_NOISY,
  STAGE_DONE
} picker_stage_t;

typedef enum { PICKER_MAIN_SEARCH, PICKER_QSEARCH } picker_type_t;

typedef struct {
  moves noisy_moves[1];
  moves quiet_moves[1];
  moves bad_noisy[1];

  uint16_t tt_move;

  uint16_t noisy_index;
  uint16_t quiet_index;
  uint16_t bad_noisy_index;

  picker_stage_t stage;
  picker_type_t type;

  position_t *pos;
  thread_t *thread;
  searchstack_t *ss;

  uint8_t skip_quiets;
  uint8_t probcut;
} move_picker_t;

// Initialize move picker for main search
void init_picker(move_picker_t *picker, position_t *pos, thread_t *thread,
                 searchstack_t *ss, uint16_t tt_move);

// Initialize move picker for quiescence search
void init_qsearch_picker(move_picker_t *picker, position_t *pos,
                         thread_t *thread, searchstack_t *ss, uint16_t tt_move);

// Initialize move picker for quiescence search
void init_probcut_picker(move_picker_t *picker, position_t *pos,
                         thread_t *thread, searchstack_t *ss);

// Get next move from picker
// Returns 0 when no more moves
uint16_t next_move(move_picker_t *picker, uint8_t skip_quiets);

#endif