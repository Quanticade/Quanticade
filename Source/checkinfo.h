#ifndef CHECKINFO_H
#define CHECKINFO_H

#include "attacks.h"
#include "bitboards.h"
#include "enums.h"
#include "move.h"
#include "structs.h"
#include <stdint.h>

typedef struct check_info {
  uint64_t check_mask[6];
  uint8_t valid;
} check_info_t;

static inline uint8_t is_direct_check(position_t *pos,
                                         check_info_t *check_info,
                                         uint16_t move) {
  if (!check_info->valid) {
    const uint8_t them = pos->side ^ 1;
    const uint8_t king_sq = get_lsb(pos->bitboards[them == white ? K : k]);
    check_info->check_mask[PAWN]   = pawn_attacks[them][king_sq];
    check_info->check_mask[KNIGHT] = knight_attacks[king_sq];
    check_info->check_mask[BISHOP] = get_bishop_attacks(king_sq, pos->occupancies[both]);
    check_info->check_mask[ROOK]   = get_rook_attacks(king_sq, pos->occupancies[both]);
    check_info->check_mask[QUEEN]  = check_info->check_mask[BISHOP] | check_info->check_mask[ROOK];
    check_info->check_mask[KING]   = 0;
    check_info->valid = 1;
  }
  return (check_info->check_mask[pos->mailbox[get_move_source(move)] % 6] >> get_move_target(move)) & 1;
}

#endif
