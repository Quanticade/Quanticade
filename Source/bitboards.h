#ifndef BITBOARDS_H
#define BITBOARDS_H

#include <assert.h>
#include <stdint.h>
#define max_ply 64

#define infinity 50000
#define mate_value 49000
#define mate_score 48000

// Population count/Hamming weight
inline int popcount(const uint64_t bb) {
    return __builtin_popcountll(bb);
}

// Returns the index of the least significant bit
inline int get_lsb(const uint64_t bb) {
    return __builtin_ctzll(bb);
}

inline int poplsb(uint64_t *bb) {
    int lsb = get_lsb(*bb);
    *bb &= *bb - 1;
    return lsb;
}

// preserve board state
#define copy_board(bitboards, occupancies, side, enpassant, castle, fifty,     \
                   hash_key, mailbox)                                          \
  uint64_t bitboards_copy[12], occupancies_copy[3];                            \
  uint8_t mailbox_copy[64];                                                    \
  int side_copy, enpassant_copy, castle_copy, fifty_copy;                      \
  memcpy(bitboards_copy, bitboards, 96);                                       \
  memcpy(occupancies_copy, occupancies, 24);                                   \
  memcpy(mailbox_copy, mailbox, 64);                                           \
  side_copy = side, enpassant_copy = enpassant, castle_copy = castle;          \
  fifty_copy = fifty;                                                          \
  uint64_t hash_key_copy = hash_key;

// restore board state
#define restore_board(bitboards, occupancies, side, enpassant, castle, fifty,  \
                      hash_key, mailbox)                                       \
  memcpy(bitboards, bitboards_copy, 96);                                       \
  memcpy(occupancies, occupancies_copy, 24);                                   \
  memcpy(mailbox, mailbox_copy, 64);                                           \
  side = side_copy, enpassant = enpassant_copy, castle = castle_copy;          \
  fifty = fifty_copy;                                                          \
  hash_key = hash_key_copy;

#define set_bit(bitboard, square) ((bitboard) |= (1ULL << (square)))
#define get_bit(bitboard, square) ((bitboard) & (1ULL << (square)))
#define pop_bit(bitboard, square) ((bitboard) &= ~(1ULL << (square)))

#define encode_move(source, target, piece, promoted, capture, double,          \
                    enpassant, castling)                                       \
  (source) | (target << 6) | (piece << 12) | (promoted << 16) |                \
      (capture << 20) | (double << 21) | (enpassant << 22) | (castling << 23)

#define get_move_source(move) (move & 0x3f)
#define get_move_target(move) ((move & 0xfc0) >> 6)
#define get_move_piece(move) ((move & 0xf000) >> 12)
#define get_move_promoted(move) ((move & 0xf0000) >> 16)
#define get_move_capture(move) (move & 0x100000)
#define get_move_double(move) (move & 0x200000)
#define get_move_enpassant(move) (move & 0x400000)
#define get_move_castling(move) (move & 0x800000)

#endif
