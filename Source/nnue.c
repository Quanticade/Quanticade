#include "nnue.h"
#include "bitboards.h"
#include "enums.h"
#include "structs.h"
#include <stdio.h>
#include <stdlib.h>

nnue_t nnue;

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
  }
}

int16_t get_white_idx(uint8_t piece, uint8_t square) {
  const size_t COLOR_STRIDE = 64 * 6;
  const size_t PIECE_STRIDE = 64;
  int piece_type = piece > 5 ? piece - 6 : piece;
  int color = piece / 6;
  int16_t white_idx =
      color * COLOR_STRIDE + piece_type * PIECE_STRIDE + (square ^ 56);
  return white_idx;
}

int16_t get_black_idx(uint8_t piece, uint8_t square) {
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

void accumulator_add(position_t *pos, uint8_t piece, uint8_t square) {
  size_t white_idx = get_white_idx(piece, square);
  size_t black_idx = get_black_idx(piece, square);

  for (int i = 0; i < HIDDEN_SIZE; ++i)
    pos->accumulator.accumulator[white][i] +=
        nnue.feature_weights[white_idx][i];

  for (int i = 0; i < HIDDEN_SIZE; ++i)
    pos->accumulator.accumulator[black][i] +=
        nnue.feature_weights[black_idx][i];
}

void accumulator_remove(position_t *pos, uint8_t piece, uint8_t square) {
  size_t white_idx = get_white_idx(piece, square);
  size_t black_idx = get_black_idx(piece, square);

  for (int i = 0; i < HIDDEN_SIZE; ++i)
    pos->accumulator.accumulator[white][i] -=
        nnue.feature_weights[white_idx][i];

  for (int i = 0; i < HIDDEN_SIZE; ++i)
    pos->accumulator.accumulator[black][i] -=
        nnue.feature_weights[black_idx][i];
}

void accumulator_addsub(position_t *pos, uint8_t piece, uint8_t from,
                        uint8_t to) {
  size_t white_idx_from = get_white_idx(piece, from);
  size_t black_idx_from = get_black_idx(piece, from);
  size_t white_idx_to = get_white_idx(piece, to);
  size_t black_idx_to = get_black_idx(piece, to);

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
    accumulator_remove(pos, pawn, from);
    accumulator_add(pos, promoted_piece, to);

    if (capture) {
      uint8_t captured_piece = mailbox[to];
      accumulator_remove(pos, captured_piece, to);
    }

    // early return to avoid unecessary iteration, because that's expensive
    return;
  }

  else if (enpass) {
    uint8_t remove_square = to + ((pos->side == white) ? -8 : 8);
    uint8_t captured_piece = mailbox[remove_square];
    accumulator_remove(pos, captured_piece, remove_square);
  }

  else if (capture) {
    uint8_t captured_piece = mailbox[to];
    accumulator_remove(pos, captured_piece, to);
  }

  // moves the piece
  accumulator_remove(pos, moving_piece, from);
  if (!promoted_piece) {
    accumulator_add(pos, moving_piece, to);
  }

  if (castling) {
    // switch target square
    switch (to) {
    // white castles king side
    case (g1):
      // move H rook
      accumulator_remove(pos, R, h1);
      accumulator_add(pos, R, f1);
      break;

    // white castles queen side
    case (c1):
      // move A rook
      accumulator_remove(pos, R, a1);
      accumulator_add(pos, R, d1);
      break;

    // black castles king side
    case (g8):
      // move H rook
      accumulator_remove(pos, r, h8);
      accumulator_add(pos, r, f8);
      break;

    // black castles queen side
    case (c8):
      // move A rook
      accumulator_remove(pos, r, a8);
      accumulator_add(pos, r, d8);
      break;
    }
  }
}
