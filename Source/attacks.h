#ifndef ATTACKS_H
#define ATTACKS_H

#include "structs.h"
#include <stdint.h>
#include "pext_magics.h"

#ifdef __BMI2__
#include <immintrin.h>  // pdep/pext
#endif

extern const int bishop_relevant_bits[64];
extern const int rook_relevant_bits[64];
extern const uint64_t rook_magic_numbers[64];
extern const uint64_t bishop_magic_numbers[64];

extern uint64_t pawn_attacks[2][64];
extern uint64_t knight_attacks[64];
extern uint64_t king_attacks[64];
extern uint64_t bishop_attacks[64][512];
extern uint64_t rook_attacks[64][4096];
extern uint64_t bishop_masks[64];
extern uint64_t rook_masks[64];
extern uint64_t file_masks[64];
extern uint64_t rank_masks[64];
extern uint64_t isolated_masks[64];
extern uint64_t white_passed_masks[64];
extern uint64_t black_passed_masks[64];

int is_square_attacked(position_t *pos, int square, int side);
uint8_t stm_in_check(position_t *pos);
void init_sliders_attacks(void);
void init_leapers_attacks(void);

// get bishop attacks
static inline uint64_t get_bishop_attacks(int square, uint64_t occupancy) {
#ifdef __BMI2__
  uint64_t attacks = BishopEmptyAttacks[square];
  if (__builtin_constant_p(occupancy) && occupancy == 0) return attacks;

  const pext_magic_t* magic = &PextMagics[square];
  size_t idx = _pext_u64(occupancy, magic->bishop_mask);
  uint16_t active = PextHeap[idx + magic->bishop_offset];
  return _pdep_u64(active, attacks);
#else
  // get bishop attacks assuming current board occupancy
  occupancy &= bishop_masks[square];
  occupancy *= bishop_magic_numbers[square];
  occupancy >>= 64 - bishop_relevant_bits[square];

  // return bishop attacks
  return bishop_attacks[square][occupancy];
#endif
}

// get rook attacks
static inline uint64_t get_rook_attacks(int square, uint64_t occupancy) {
#ifdef __BMI2__
  uint64_t attacks = RookEmptyAttacks[square];
  if (__builtin_constant_p(occupancy) && occupancy == 0) return attacks;

  const pext_magic_t* magic = &PextMagics[square];
  size_t idx = _pext_u64(occupancy, magic->rook_mask);
  uint16_t active = PextHeap[idx + magic->rook_offset];
  return _pdep_u64(active, attacks);
#else
  // get rook attacks assuming current board occupancy
  occupancy &= rook_masks[square];
  occupancy *= rook_magic_numbers[square];
  occupancy >>= 64 - rook_relevant_bits[square];

  // return rook attacks
  return rook_attacks[square][occupancy];
#endif
}

// get queen attacks
static inline uint64_t get_queen_attacks(int square, uint64_t occupancy) {
  return get_bishop_attacks(square, occupancy) | get_rook_attacks(square, occupancy);
}

static inline uint64_t get_pawn_attacks(uint8_t side, int square) {
  return pawn_attacks[side][square];
}

static inline uint64_t get_knight_attacks(int square) {
  return knight_attacks[square];
}

static inline uint64_t get_king_attacks(int square) {
  return king_attacks[square];
}

uint64_t attackers_to(position_t *pos, int square, uint64_t occupancy);

void calculate_threats(position_t *pos, searchstack_t *ss);

uint8_t is_square_threatened(searchstack_t *ss, int square);

#endif
