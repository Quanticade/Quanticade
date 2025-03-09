#ifndef BITBOARDS_H
#define BITBOARDS_H

#include <assert.h>
#include <stdint.h>

#define NO_SCORE 32001
#define INF 32000
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

// preserve board state
#define copy_board(bitboards, occupancies, side, enpassant, castle, fifty,     \
                   hash_key, pawn_key, mailbox)                                \
  uint64_t bitboards_copy[12], occupancies_copy[3];                            \
  uint8_t mailbox_copy[64];                                                    \
  int side_copy, enpassant_copy, castle_copy, fifty_copy;                      \
  memcpy(bitboards_copy, bitboards, 96);                                       \
  memcpy(occupancies_copy, occupancies, 24);                                   \
  memcpy(mailbox_copy, mailbox, 64);                                           \
  side_copy = side, enpassant_copy = enpassant, castle_copy = castle;          \
  fifty_copy = fifty;                                                          \
  uint64_t hash_key_copy = hash_key;                                           \
  uint64_t pawn_key_copy = pawn_key;

// restore board state
#define restore_board(bitboards, occupancies, side, enpassant, castle, fifty,  \
                      hash_key, pawn_key, mailbox)                             \
  memcpy(bitboards, bitboards_copy, 96);                                       \
  memcpy(occupancies, occupancies_copy, 24);                                   \
  memcpy(mailbox, mailbox_copy, 64);                                           \
  side = side_copy, enpassant = enpassant_copy, castle = castle_copy;          \
  fifty = fifty_copy;                                                          \
  hash_key = hash_key_copy;                                                    \
  pawn_key = pawn_key_copy

#define set_bit(bitboard, square) ((bitboard) |= (1ULL << (square)))
#define get_bit(bitboard, square) ((bitboard) & (1ULL << (square)))
#define pop_bit(bitboard, square) ((bitboard) &= ~(1ULL << (square)))

#endif
