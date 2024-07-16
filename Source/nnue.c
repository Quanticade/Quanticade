#include "nnue.h"
#include "bitboards.h"
#include "enums.h"
#include "structs.h"
#include <stdio.h>
#include <stdlib.h>

nnue_t nnue;
accumulator_t accumulator;

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

int nnue_eval_pos(position_t *pos) {
  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    accumulator.accumulator[0][i] = nnue.feature_bias[i];
    accumulator.accumulator[1][i] = nnue.feature_bias[i];
  }

  for (int i = 0; i < HIDDEN_SIZE; ++i)

    for (int color = 0; color < 2; ++color) {
      for (int piece = 0; piece <= KING; ++piece) {
        uint64_t bitboard = pos->bitboards[piece + (color == white ? 0 : 6)];
        while (bitboard) {
           uint64_t square =__builtin_ctzll(bitboard);
          // nnue uses a8 = 0, while we want a1 = 0, so we flip the white square
          int black_square = get_lsb(bitboard);
          int white_square = black_square ^ 56;
          int nnue_white_piece = piece + (color == white ? 0 : 6);

          int nnue_black_piece = piece + ((color ^ 1) == white ? 0 : 6);

          int nnue_white_input_index = 64 * nnue_white_piece + white_square;
          int nnue_black_input_index = 64 * nnue_black_piece + black_square;

          // updates all the pieces in the accumulators
          for (int i = 0; i < HIDDEN_SIZE; ++i)
            accumulator.accumulator[white][i] +=
                nnue.feature_weights[nnue_white_input_index][i];

          for (int i = 0; i < HIDDEN_SIZE; ++i)
            accumulator.accumulator[black][i] +=
                nnue.feature_weights[nnue_black_input_index][i];

          pop_bit(bitboard, square);
        }
      }
    }

  int eval = 0;
  // feed everything forward to get the final value
  for (int i = 0; i < HIDDEN_SIZE; ++i)
    eval +=
        screlu(accumulator.accumulator[pos->side][i]) * nnue.output_weights[0][i];

  for (int i = 0; i < HIDDEN_SIZE; ++i)
    eval += screlu(accumulator.accumulator[pos->side ^ 1][i]) * nnue.output_weights[1][i];

  eval /= L1Q;
  eval += nnue.output_bias;
  eval = (eval * SCALE) / (L1Q * OutputQ);

  return eval;
}
