#ifndef BITBOARDS_H
#define BITBOARDS_H

#include <assert.h>
#include <stdint.h>

#define INF 32000
#define NO_SCORE -INF
#define MATE_VALUE 31000
#define MATE_SCORE 30000

// Population count/Hamming weight
static inline int popcount(const uint64_t bb) {
  return __builtin_popcountll(bb);
}

// Returns the index of the least significant bit
static inline int get_lsb(const uint64_t bb) { return __builtin_ctzll(bb); }

static inline int poplsb(uint64_t *bb) {
  int lsb = get_lsb(*bb);
  *bb &= *bb - 1;
  return lsb;
}

#define set_bit(bitboard, square) ((bitboard) |= (1ULL << (square)))
#define get_bit(bitboard, square) ((bitboard) & (1ULL << (square)))
#define pop_bit(bitboard, square) ((bitboard) &= ~(1ULL << (square)))

#endif
