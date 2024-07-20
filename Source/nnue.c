#include "nnue.h"
#include "bitboards.h"
#include "enums.h"
#include "incbin/incbin.h"
#include "structs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

nnue_t nnue;

#if !defined(_MSC_VER)
INCBIN(EVAL, EVALFILE);
#else
const unsigned char gEVALData[1] = {};
const unsigned char *const gEVALEnd = &gEVALData[1];
const unsigned int gEVALSize = 1;
#endif

int32_t clamp_int32(int32_t d, int32_t min, int32_t max) {
  const int32_t t = d < min ? min : d;
  return t > max ? max : t;
}

inline int32_t screlu(int16_t value) {
  const int32_t clipped = clamp_int32((int32_t)value, 0, L1Q);
  return clipped * clipped;
}

void nnue_init(const char *nnue_file_name) {
  // open the nn file
  FILE *nn = fopen(nnue_file_name, "rb");

  // if it's not invalid read the config values from it
  if (nn) {
    // initialize an accumulator for every input of the second layer
    size_t read = 0;
    size_t fileSize = sizeof(nnue_t);
    size_t objectsExpected = fileSize / sizeof(int16_t);

    read += fread(nnue.feature_weights, sizeof(int16_t),
                  INPUT_WEIGHTS * HIDDEN_SIZE, nn);
    read += fread(nnue.feature_bias, sizeof(int16_t), HIDDEN_SIZE, nn);
    read += fread(nnue.output_weights, sizeof(int16_t), HIDDEN_SIZE * 2, nn);
    read += fread(&nnue.output_bias, sizeof(int16_t), 1, nn);

    if (read != objectsExpected) {
      printf("Error loading the net, aborting\n");
      exit(1);
    }

    // after reading the config we can close the file
    fclose(nn);
  } else {
    uint64_t memoryIndex = 0;
    memcpy(nnue.feature_weights, &gEVALData[memoryIndex],
           INPUT_WEIGHTS * HIDDEN_SIZE * sizeof(int16_t));
    memoryIndex += INPUT_WEIGHTS * HIDDEN_SIZE * sizeof(int16_t);
    memcpy(nnue.feature_bias, &gEVALData[memoryIndex],
           HIDDEN_SIZE * sizeof(int16_t));
    memoryIndex += HIDDEN_SIZE * sizeof(int16_t);

    memcpy(nnue.output_weights, &gEVALData[memoryIndex],
           HIDDEN_SIZE * sizeof(int16_t) * 2);
    memoryIndex += HIDDEN_SIZE * sizeof(int16_t) * 2;
    memcpy(&nnue.output_bias, &gEVALData[memoryIndex], 1 * sizeof(int16_t));
  }
}

static inline int16_t get_white_idx(uint8_t piece, uint8_t square) {
  const size_t COLOR_STRIDE = 64 * 6;
  const size_t PIECE_STRIDE = 64;
  int piece_type = piece > 5 ? piece - 6 : piece;
  int color = piece / 6;
  int16_t white_idx =
      color * COLOR_STRIDE + piece_type * PIECE_STRIDE + (square ^ 56);
  return white_idx;
}

static inline int16_t get_black_idx(uint8_t piece, uint8_t square) {
  const size_t COLOR_STRIDE = 64 * 6;
  const size_t PIECE_STRIDE = 64;
  int piece_type = piece > 5 ? piece - 6 : piece;
  int color = piece / 6;
  int16_t black_idx =
      (1 ^ color) * COLOR_STRIDE + piece_type * PIECE_STRIDE + square;
  return black_idx;
}

void init_accumulator(position_t *pos) {
  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    pos->accumulator.accumulator[0][i] = nnue.feature_bias[i];
    pos->accumulator.accumulator[1][i] = nnue.feature_bias[i];
  }

  for (int piece = P; piece <= k; ++piece) {
    uint64_t bitboard = pos->bitboards[piece];
    while (bitboard) {
      int square = get_lsb(bitboard);
      size_t white_idx = get_white_idx(piece, square);
      size_t black_idx = get_black_idx(piece, square);

      // updates all the pieces in the accumulators
      for (int i = 0; i < HIDDEN_SIZE; ++i)
        pos->accumulator.accumulator[white][i] +=
            nnue.feature_weights[white_idx][i];

      for (int i = 0; i < HIDDEN_SIZE; ++i)
        pos->accumulator.accumulator[black][i] +=
            nnue.feature_weights[black_idx][i];

      pop_bit(bitboard, square);
    }
  }
}

int nnue_eval_pos(position_t *pos) {
  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    pos->accumulator.accumulator[0][i] = nnue.feature_bias[i];
    pos->accumulator.accumulator[1][i] = nnue.feature_bias[i];
  }

  for (int piece = P; piece <= k; ++piece) {
    uint64_t bitboard = pos->bitboards[piece];
    while (bitboard) {
      int square = get_lsb(bitboard);
      int16_t white_idx = get_white_idx(piece, square);
      int16_t black_idx = get_black_idx(piece, square);

      // updates all the pieces in the accumulators
      for (int i = 0; i < HIDDEN_SIZE; ++i)
        pos->accumulator.accumulator[white][i] +=
            nnue.feature_weights[white_idx][i];

      for (int i = 0; i < HIDDEN_SIZE; ++i)
        pos->accumulator.accumulator[black][i] +=
            nnue.feature_weights[black_idx][i];

      pop_bit(bitboard, square);
    }
  }

  int eval = 0;
  // feed everything forward to get the final value
  for (int i = 0; i < HIDDEN_SIZE; ++i)
    eval += screlu(pos->accumulator.accumulator[pos->side][i]) *
            nnue.output_weights[0][i];

  for (int i = 0; i < HIDDEN_SIZE; ++i)
    eval += screlu(pos->accumulator.accumulator[pos->side ^ 1][i]) *
            nnue.output_weights[1][i];

  eval /= L1Q;
  eval += nnue.output_bias;
  eval = (eval * SCALE) / (L1Q * OutputQ);

  return eval;
}

int nnue_evaluate(position_t *pos) {
  int eval = 0;
  // feed everything forward to get the final value
  for (int i = 0; i < HIDDEN_SIZE; ++i)
    eval += screlu(pos->accumulator.accumulator[pos->side][i]) *
            nnue.output_weights[0][i];

  for (int i = 0; i < HIDDEN_SIZE; ++i)
    eval += screlu(pos->accumulator.accumulator[pos->side ^ 1][i]) *
            nnue.output_weights[1][i];

  eval /= L1Q;
  eval += nnue.output_bias;
  eval = (eval * SCALE) / (L1Q * OutputQ);

  return eval;
}

static inline void accumulator_addsub(position_t *pos, uint8_t piece1,
                                      uint8_t piece2, uint8_t from1,
                                      uint8_t to2) {
  size_t white_idx_from = get_white_idx(piece1, from1);
  size_t black_idx_from = get_black_idx(piece1, from1);
  size_t white_idx_to = get_white_idx(piece2, to2);
  size_t black_idx_to = get_black_idx(piece2, to2);

  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    pos->accumulator.accumulator[white][i] =
        pos->accumulator.accumulator[white][i] -
        nnue.feature_weights[white_idx_from][i] +
        nnue.feature_weights[white_idx_to][i];
    pos->accumulator.accumulator[black][i] =
        pos->accumulator.accumulator[black][i] -
        nnue.feature_weights[black_idx_from][i] +
        nnue.feature_weights[black_idx_to][i];
  }
}

static inline void accumulator_addsubsub(position_t *pos, uint8_t piece1,
                                         uint8_t piece2, uint8_t piece3,
                                         uint8_t from1, uint8_t from2,
                                         uint8_t to3) {
  size_t white_idx_from1 = get_white_idx(piece1, from1);
  size_t black_idx_from1 = get_black_idx(piece1, from1);
  size_t white_idx_from2 = get_white_idx(piece2, from2);
  size_t black_idx_from2 = get_black_idx(piece2, from2);
  size_t white_idx_to = get_white_idx(piece3, to3);
  size_t black_idx_to = get_black_idx(piece3, to3);

  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    pos->accumulator.accumulator[white][i] =
        pos->accumulator.accumulator[white][i] -
        nnue.feature_weights[white_idx_from1][i] -
        nnue.feature_weights[white_idx_from2][i] +
        nnue.feature_weights[white_idx_to][i];
    pos->accumulator.accumulator[black][i] =
        pos->accumulator.accumulator[black][i] -
        nnue.feature_weights[black_idx_from1][i] -
        nnue.feature_weights[black_idx_from2][i] +
        nnue.feature_weights[black_idx_to][i];
  }
}

static inline void accumulator_addaddsubsub(position_t *pos, uint8_t piece1,
                                            uint8_t piece2, uint8_t piece3,
                                            uint8_t piece4, uint8_t from1,
                                            uint8_t from2, uint8_t to3,
                                            uint8_t to4) {
  size_t white_idx_from1 = get_white_idx(piece1, from1);
  size_t black_idx_from1 = get_black_idx(piece1, from1);
  size_t white_idx_from2 = get_white_idx(piece2, from2);
  size_t black_idx_from2 = get_black_idx(piece2, from2);
  size_t white_idx_to1 = get_white_idx(piece3, to3);
  size_t black_idx_to1 = get_black_idx(piece3, to3);
  size_t white_idx_to2 = get_white_idx(piece4, to4);
  size_t black_idx_to2 = get_black_idx(piece4, to4);

  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    pos->accumulator.accumulator[white][i] =
        pos->accumulator.accumulator[white][i] -
        nnue.feature_weights[white_idx_from1][i] -
        nnue.feature_weights[white_idx_from2][i] +
        nnue.feature_weights[white_idx_to1][i] +
        nnue.feature_weights[white_idx_to2][i];
    pos->accumulator.accumulator[black][i] =
        pos->accumulator.accumulator[black][i] -
        nnue.feature_weights[black_idx_from1][i] -
        nnue.feature_weights[black_idx_from2][i] +
        nnue.feature_weights[black_idx_to1][i] +
        nnue.feature_weights[black_idx_to2][i];
  }
}

void accumulator_make_move(position_t *pos, int move, uint8_t *mailbox) {
  int from = get_move_source(move);
  int to = get_move_target(move);
  int moving_piece = get_move_piece(move);
  int promoted_piece = get_move_promoted(move);
  int capture = get_move_capture(move);
  int enpass = get_move_enpassant(move);
  int castling = get_move_castling(move);

  if (promoted_piece) {
    uint8_t pawn = pos->side == 0 ? p : P;
    if (capture) {
      uint8_t captured_piece = mailbox[to];
      accumulator_addsubsub(pos, pawn, captured_piece, promoted_piece, from, to,
                            to);
    } else {
      accumulator_addsub(pos, pawn, promoted_piece, from, to);
    }
  }

  else if (enpass) {
    uint8_t remove_square = to + ((pos->side == white) ? -8 : 8);
    uint8_t captured_piece = mailbox[remove_square];
    accumulator_addsubsub(pos, captured_piece, moving_piece, moving_piece,
                          remove_square, from, to);
  }

  else if (capture) {
    uint8_t captured_piece = mailbox[to];
    accumulator_addsubsub(pos, captured_piece, moving_piece, moving_piece, to,
                          from, to);
  }

  else if (castling) {
    // switch target square
    switch (to) {
    // white castles king side
    case (g1):
      // move H rook
      accumulator_addaddsubsub(pos, R, moving_piece, R, moving_piece, h1, from,
                               f1, to);
      break;

    // white castles queen side
    case (c1):
      // move A rook
      accumulator_addaddsubsub(pos, R, moving_piece, R, moving_piece, a1, from,
                               d1, to);
      break;

    // black castles king side
    case (g8):
      // move H rook
      accumulator_addaddsubsub(pos, r, moving_piece, r, moving_piece, h8, from,
                               f8, to);
      break;

    // black castles queen side
    case (c8):
      // move A rook
      accumulator_addaddsubsub(pos, r, moving_piece, r, moving_piece, a8, from,
                               d8, to);
      break;
    }
  } else {
    accumulator_addsub(pos, moving_piece, moving_piece, from, to);
  }
}
