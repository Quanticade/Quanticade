#include "nnue.h"
#include "bitboards.h"
#include "enums.h"
#include "incbin/incbin.h"
#include "move.h"
#include "simd.h"
#include "structs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

nnue_t nnue;

uint8_t buckets[64] = {12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
                       12, 12, 12, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
                       11, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10,
                       10, 8,  8,  9,  9,  9,  9,  8,  8,  4,  5,  6,  7,
                       7,  6,  5,  4,  0,  1,  2,  3,  3,  2,  1,  0};

#if !defined(_MSC_VER)
INCBIN(EVAL, EVALFILE);
#else
const unsigned char gEVALData[1] = {};
const unsigned char *const gEVALEnd = &gEVALData[1];
const unsigned int gEVALSize = 1;
#endif

const uint8_t BUCKET_DIVISOR = (32 + OUTPUT_BUCKETS - 1) / OUTPUT_BUCKETS;

uint8_t get_king_bucket(uint8_t side, uint8_t square) {
  return buckets[side ? square ^ 56 : square];
}

uint8_t need_refresh(uint8_t *mailbox, uint16_t move) {
  uint8_t moved_piece = mailbox[get_move_source(move)];
  if (moved_piece == k || moved_piece == K) {
    uint8_t side = moved_piece >= 6;
    uint8_t source_flip = (get_move_source(move) & 7) >= 4;
    uint8_t target_flip = (get_move_target(move) & 7) >= 4;
    if ((get_king_bucket(side, get_move_source(move)) !=
         get_king_bucket(side, get_move_target(move))) ||
        source_flip != target_flip) {
      return 1;
    }
    return 0;
  }
  return 0;
}

static int32_t clamp_int32(int32_t d, int32_t min, int32_t max) {
  const int32_t t = d < min ? min : d;
  return t > max ? max : t;
}

static inline int32_t screlu(int16_t value) {
  const int32_t clipped = clamp_int32((int32_t)value, 0, L1Q);
  return clipped * clipped;
}

static inline uint8_t calculate_output_bucket(position_t *pos) {
  uint8_t pieces = popcount(pos->occupancies[2]);
  return (pieces - 2) / 4;
}

int nnue_init_incbin(void) {
  uint64_t memoryIndex = 0;
  char version_string[21];
  memcpy(version_string, &gEVALData[memoryIndex], 20 * sizeof(char));
  version_string[20] = '\0';
  if (strcmp(version_string, "4275636B657432303438") != 0) {
    return 0;
  }
  memoryIndex += 20 * sizeof(char);
  memcpy(nnue.feature_weights, &gEVALData[memoryIndex],
         INPUT_WEIGHTS * HIDDEN_SIZE * KING_BUCKETS * sizeof(int16_t));
  memoryIndex += INPUT_WEIGHTS * HIDDEN_SIZE * KING_BUCKETS * sizeof(int16_t);
  memcpy(nnue.feature_bias, &gEVALData[memoryIndex],
         HIDDEN_SIZE * sizeof(int16_t));
  memoryIndex += HIDDEN_SIZE * sizeof(int16_t);

  memcpy(&nnue.output_weights, &gEVALData[memoryIndex],
         HIDDEN_SIZE * sizeof(int16_t) * 2 * OUTPUT_BUCKETS);
  memoryIndex += HIDDEN_SIZE * sizeof(int16_t) * 2 * OUTPUT_BUCKETS;
  memcpy(&nnue.output_bias, &gEVALData[memoryIndex],
         OUTPUT_BUCKETS * sizeof(int16_t));
  return 1;
}

void nnue_init(const char *nnue_file_name) {
  // open the nn file
  FILE *nn = fopen(nnue_file_name, "rb");

  // if it's not invalid read the config values from it
  if (nn) {
    // initialize an accumulator for every input of the second layer
    size_t read = 0;
    size_t fileSize = sizeof(nnue_t);
    size_t objectsExpected = (fileSize / sizeof(int16_t)) + 20 -
                             24; // Due to alligment we deduct 24 from it
    char version_string[21];
    read += fread(version_string, sizeof(char), 20, nn);
    version_string[20] = '\0';
    (void)version_string;

    if (strcmp(version_string, "4275636B657432303438") != 0) {
      printf("Imcompatible NNUE file. Trying to load NNUE from binary\n");
      fclose(nn);
      if (nnue_init_incbin() == 0) {
        printf("Failed to load network from incbin. Exiting\n");
        exit(1);
      }
    } else {

      read += fread(nnue.feature_weights, sizeof(int16_t),
                    INPUT_WEIGHTS * HIDDEN_SIZE * KING_BUCKETS, nn);
      read += fread(nnue.feature_bias, sizeof(int16_t), HIDDEN_SIZE, nn);
      read += fread(nnue.output_weights, sizeof(int16_t),
                    HIDDEN_SIZE * 2 * OUTPUT_BUCKETS, nn);
      read += fread(&nnue.output_bias, sizeof(int16_t), OUTPUT_BUCKETS, nn);

      if (read != objectsExpected) {
        printf("We read: %zu but the expected is %zu\n", read, objectsExpected);
        printf("Error loading the net, aborting\n");
        exit(1);
      }

      // after reading the config we can close the file
      fclose(nn);
    }
  } else {
    if (nnue_init_incbin() == 0) {
      printf("Failed to load network from incbin. Exiting\n");
      exit(1);
    }
  }
}

static inline int16_t get_white_idx(uint8_t piece, uint8_t square,
                                    uint8_t white_king_square) {
  const size_t COLOR_STRIDE = 64 * 6;
  const size_t PIECE_STRIDE = 64;
  if ((white_king_square & 7) >= 4) {
    square = square ^ 7;
  }
  int piece_type = piece > 5 ? piece - 6 : piece;
  int color = piece / 6;
  int16_t white_idx =
      color * COLOR_STRIDE + piece_type * PIECE_STRIDE + (square ^ 56);
  return white_idx;
}

static inline int16_t get_black_idx(uint8_t piece, uint8_t square,
                                    uint8_t black_king_square) {
  const size_t COLOR_STRIDE = 64 * 6;
  const size_t PIECE_STRIDE = 64;
  if ((black_king_square & 7) >= 4) {
    square = square ^ 7;
  }
  int piece_type = piece > 5 ? piece - 6 : piece;
  int color = piece / 6;
  int16_t black_idx =
      (1 ^ color) * COLOR_STRIDE + piece_type * PIECE_STRIDE + square;
  return black_idx;
}

void refresh_white_accumulator(position_t *pos, accumulator_t *accumulator) {
  uint8_t white_bucket = get_king_bucket(white, get_lsb(pos->bitboards[K]));
  for (int i = 0; i < HIDDEN_SIZE; ++i)
    accumulator->accumulator[white][i] = nnue.feature_bias[i];

  for (int piece = P; piece <= k; ++piece) {
    uint64_t bitboard = pos->bitboards[piece];
    while (bitboard) {
      int square = get_lsb(bitboard);
      size_t white_idx =
          get_white_idx(piece, square, get_lsb(pos->bitboards[K]));

      for (int i = 0; i < HIDDEN_SIZE; ++i)
        accumulator->accumulator[white][i] +=
            nnue.feature_weights[white_bucket][white_idx][i];

      pop_bit(bitboard, square);
    }
  }
}

void refresh_black_accumulator(position_t *pos, accumulator_t *accumulator) {
  uint8_t black_bucket = get_king_bucket(black, get_lsb(pos->bitboards[k]));
  for (int i = 0; i < HIDDEN_SIZE; ++i)
    accumulator->accumulator[black][i] = nnue.feature_bias[i];

  for (int piece = P; piece <= k; ++piece) {
    uint64_t bitboard = pos->bitboards[piece];
    while (bitboard) {
      int square = get_lsb(bitboard);
      size_t black_idx =
          get_black_idx(piece, square, get_lsb(pos->bitboards[k]));

      for (int i = 0; i < HIDDEN_SIZE; ++i)
        accumulator->accumulator[black][i] +=
            nnue.feature_weights[black_bucket][black_idx][i];

      pop_bit(bitboard, square);
    }
  }
}

void init_accumulator(position_t *pos, accumulator_t *accumulator) {
  uint8_t white_bucket = get_king_bucket(white, get_lsb(pos->bitboards[K]));
  uint8_t black_bucket = get_king_bucket(black, get_lsb(pos->bitboards[k]));
  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    accumulator->accumulator[0][i] = nnue.feature_bias[i];
    accumulator->accumulator[1][i] = nnue.feature_bias[i];
  }

  for (int piece = P; piece <= k; ++piece) {
    uint64_t bitboard = pos->bitboards[piece];
    while (bitboard) {
      int square = get_lsb(bitboard);
      size_t white_idx =
          get_white_idx(piece, square, get_lsb(pos->bitboards[K]));
      size_t black_idx =
          get_black_idx(piece, square, get_lsb(pos->bitboards[k]));

      // updates all the pieces in the accumulators
      for (int i = 0; i < HIDDEN_SIZE; ++i)
        accumulator->accumulator[white][i] +=
            nnue.feature_weights[white_bucket][white_idx][i];

      for (int i = 0; i < HIDDEN_SIZE; ++i)
        accumulator->accumulator[black][i] +=
            nnue.feature_weights[black_bucket][black_idx][i];

      pop_bit(bitboard, square);
    }
  }
}

int nnue_eval_pos(position_t *pos, accumulator_t *accumulator) {
  uint8_t white_bucket = get_king_bucket(white, get_lsb(pos->bitboards[K]));
  uint8_t black_bucket = get_king_bucket(black, get_lsb(pos->bitboards[k]));
  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    accumulator->accumulator[0][i] = nnue.feature_bias[i];
    accumulator->accumulator[1][i] = nnue.feature_bias[i];
  }

  for (int piece = P; piece <= k; ++piece) {
    uint64_t bitboard = pos->bitboards[piece];
    while (bitboard) {
      int square = get_lsb(bitboard);
      int16_t white_idx =
          get_white_idx(piece, square, get_lsb(pos->bitboards[K]));
      int16_t black_idx =
          get_black_idx(piece, square, get_lsb(pos->bitboards[k]));

      // updates all the pieces in the accumulators
      for (int i = 0; i < HIDDEN_SIZE; ++i)
        accumulator->accumulator[white][i] +=
            nnue.feature_weights[white_bucket][white_idx][i];

      for (int i = 0; i < HIDDEN_SIZE; ++i)
        accumulator->accumulator[black][i] +=
            nnue.feature_weights[black_bucket][black_idx][i];

      pop_bit(bitboard, square);
    }
  }

  int eval = 0;
  uint8_t bucket = calculate_output_bucket(pos);
  // feed everything forward to get the final value
  for (int i = 0; i < HIDDEN_SIZE; ++i)
    eval += screlu(accumulator->accumulator[pos->side][i]) *
            nnue.output_weights[bucket][0][i];

  for (int i = 0; i < HIDDEN_SIZE; ++i)
    eval += screlu(accumulator->accumulator[pos->side ^ 1][i]) *
            nnue.output_weights[bucket][1][i];

  eval /= L1Q;
  eval += nnue.output_bias[bucket];
  eval = (eval * SCALE) / (L1Q * OutputQ);

  return eval;
}

int nnue_evaluate(position_t *pos, accumulator_t *accumulator) {
  int eval = 0;
  uint8_t side = pos->side;
  uint8_t bucket = calculate_output_bucket(pos);

#if defined(USE_SIMD)
  vepi32 sum = zero_epi32();
  const int chunk_size = sizeof(vepi16) / sizeof(int16_t);

  for (int i = 0; i < HIDDEN_SIZE; i += chunk_size) {
    const vepi16 accumulator_data =
        load_epi16(&accumulator->accumulator[side][i]);
    const vepi16 weights = load_epi16(&nnue.output_weights[bucket][0][i]);

    const vepi16 clipped_accumulator = clip(accumulator_data, L1Q);

    const vepi16 intermediate = multiply_epi16(clipped_accumulator, weights);

    const vepi32 result = multiply_add_epi16(intermediate, clipped_accumulator);

    sum = add_epi32(sum, result);
  }

  for (int i = 0; i < HIDDEN_SIZE; i += chunk_size) {
    const vepi16 accumulator_data =
        load_epi16(&accumulator->accumulator[side ^ 1][i]);
    const vepi16 weights = load_epi16(&nnue.output_weights[bucket][1][i]);

    const vepi16 clipped_accumulator = clip(accumulator_data, L1Q);

    const vepi16 intermediate = multiply_epi16(clipped_accumulator, weights);

    const vepi32 result = multiply_add_epi16(intermediate, clipped_accumulator);

    sum = add_epi32(sum, result);
  }

  eval = reduce_add_epi32(sum);
#else
  // feed everything forward to get the final value
  for (int i = 0; i < HIDDEN_SIZE; ++i)
    eval += screlu(accumulator->accumulator[side][i]) *
            nnue.output_weights[bucket][0][i];

  for (int i = 0; i < HIDDEN_SIZE; ++i)
    eval += screlu(accumulator->accumulator[side ^ 1][i]) *
            nnue.output_weights[bucket][1][i];
#endif
  eval /= L1Q;
  eval += nnue.output_bias[bucket];
  eval = (eval * SCALE) / (L1Q * OutputQ);

  return eval;
}

static inline void
accumulator_addsub(accumulator_t *accumulator, accumulator_t *prev_accumulator,
                   uint8_t white_king_square, uint8_t black_king_square,
                   uint8_t white_bucket, uint8_t black_bucket, uint8_t piece1,
                   uint8_t piece2, uint8_t from1, uint8_t to2,
                   uint8_t color_flag) {
  size_t white_idx_from = get_white_idx(piece1, from1, white_king_square);
  size_t black_idx_from = get_black_idx(piece1, from1, black_king_square);
  size_t white_idx_to = get_white_idx(piece2, to2, white_king_square);
  size_t black_idx_to = get_black_idx(piece2, to2, black_king_square);

  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    if (color_flag == 0 || color_flag == 2) {
      accumulator->accumulator[white][i] =
          prev_accumulator->accumulator[white][i] -
          nnue.feature_weights[white_bucket][white_idx_from][i] +
          nnue.feature_weights[white_bucket][white_idx_to][i];
    }
    if (color_flag == 1 || color_flag == 2) {
      accumulator->accumulator[black][i] =
          prev_accumulator->accumulator[black][i] -
          nnue.feature_weights[black_bucket][black_idx_from][i] +
          nnue.feature_weights[black_bucket][black_idx_to][i];
    }
  }
}

static inline void accumulator_addsubsub(
    accumulator_t *accumulator, accumulator_t *prev_accumulator,
    uint8_t white_king_square, uint8_t black_king_square, uint8_t white_bucket,
    uint8_t black_bucket, uint8_t piece1, uint8_t piece2, uint8_t piece3,
    uint8_t from1, uint8_t from2, uint8_t to3, uint8_t color_flag) {
  size_t white_idx_from1 = get_white_idx(piece1, from1, white_king_square);
  size_t black_idx_from1 = get_black_idx(piece1, from1, black_king_square);
  size_t white_idx_from2 = get_white_idx(piece2, from2, white_king_square);
  size_t black_idx_from2 = get_black_idx(piece2, from2, black_king_square);
  size_t white_idx_to = get_white_idx(piece3, to3, white_king_square);
  size_t black_idx_to = get_black_idx(piece3, to3, black_king_square);

  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    if (color_flag == 0 || color_flag == 2) {
      accumulator->accumulator[white][i] =
          prev_accumulator->accumulator[white][i] -
          nnue.feature_weights[white_bucket][white_idx_from1][i] -
          nnue.feature_weights[white_bucket][white_idx_from2][i] +
          nnue.feature_weights[white_bucket][white_idx_to][i];
    }
    if (color_flag == 1 || color_flag == 2) {
      accumulator->accumulator[black][i] =
          prev_accumulator->accumulator[black][i] -
          nnue.feature_weights[black_bucket][black_idx_from1][i] -
          nnue.feature_weights[black_bucket][black_idx_from2][i] +
          nnue.feature_weights[black_bucket][black_idx_to][i];
    }
  }
}

static inline void accumulator_addaddsubsub(
    accumulator_t *accumulator, accumulator_t *prev_accumulator,
    uint8_t white_king_square, uint8_t black_king_square, uint8_t white_bucket,
    uint8_t black_bucket, uint8_t piece1, uint8_t piece2, uint8_t piece3,
    uint8_t piece4, uint8_t from1, uint8_t from2, uint8_t to3, uint8_t to4,
    uint8_t color_flag) {
  size_t white_idx_from1 = get_white_idx(piece1, from1, white_king_square);
  size_t black_idx_from1 = get_black_idx(piece1, from1, black_king_square);
  size_t white_idx_from2 = get_white_idx(piece2, from2, white_king_square);
  size_t black_idx_from2 = get_black_idx(piece2, from2, black_king_square);
  size_t white_idx_to1 = get_white_idx(piece3, to3, white_king_square);
  size_t black_idx_to1 = get_black_idx(piece3, to3, black_king_square);
  size_t white_idx_to2 = get_white_idx(piece4, to4, white_king_square);
  size_t black_idx_to2 = get_black_idx(piece4, to4, black_king_square);

  for (int i = 0; i < HIDDEN_SIZE; ++i) {
    if (color_flag == 0 || color_flag == 2) {
      accumulator->accumulator[white][i] =
          prev_accumulator->accumulator[white][i] -
          nnue.feature_weights[white_bucket][white_idx_from1][i] -
          nnue.feature_weights[white_bucket][white_idx_from2][i] +
          nnue.feature_weights[white_bucket][white_idx_to1][i] +
          nnue.feature_weights[white_bucket][white_idx_to2][i];
    }
    if (color_flag == 1 || color_flag == 2) {
      accumulator->accumulator[black][i] =
          prev_accumulator->accumulator[black][i] -
          nnue.feature_weights[black_bucket][black_idx_from1][i] -
          nnue.feature_weights[black_bucket][black_idx_from2][i] +
          nnue.feature_weights[black_bucket][black_idx_to1][i] +
          nnue.feature_weights[black_bucket][black_idx_to2][i];
    }
  }
}

void accumulator_make_move(accumulator_t *accumulator,
                           accumulator_t *prev_accumulator,
                           uint8_t white_king_square, uint8_t black_king_square,
                           uint8_t white_bucket, uint8_t black_bucket,
                           uint8_t side, int move, uint8_t *mailbox,
                           uint8_t color_flag) {
  int from = get_move_source(move);
  int to = get_move_target(move);
  int moving_piece = mailbox[from];
  int promoted_piece = get_move_promoted(!side, move);
  int capture = get_move_capture(move);
  int enpass = get_move_enpassant(move);
  int castling = get_move_castling(move);

  if (promoted_piece) {
    uint8_t pawn = side == 0 ? p : P;
    if (capture) {
      uint8_t captured_piece = mailbox[to];
      accumulator_addsubsub(accumulator, prev_accumulator, white_king_square,
                            black_king_square, white_bucket, black_bucket, pawn,
                            captured_piece, promoted_piece, from, to, to,
                            color_flag);
    } else {
      accumulator_addsub(accumulator, prev_accumulator, white_king_square,
                         black_king_square, white_bucket, black_bucket, pawn,
                         promoted_piece, from, to, color_flag);
    }
  }

  else if (enpass) {
    uint8_t remove_square = to + ((side == white) ? -8 : 8);
    uint8_t captured_piece = mailbox[remove_square];
    accumulator_addsubsub(accumulator, prev_accumulator, white_king_square,
                          black_king_square, white_bucket, black_bucket,
                          captured_piece, moving_piece, moving_piece,
                          remove_square, from, to, color_flag);
  }

  else if (capture) {
    uint8_t captured_piece = mailbox[to];
    accumulator_addsubsub(accumulator, prev_accumulator, white_king_square,
                          black_king_square, white_bucket, black_bucket,
                          captured_piece, moving_piece, moving_piece, to, from,
                          to, color_flag);
  }

  else if (castling) {
    // switch target square
    switch (to) {
    // white castles king side
    case (g1):
      // move H rook
      accumulator_addaddsubsub(accumulator, prev_accumulator, white_king_square,
                               black_king_square, white_bucket, black_bucket, R,
                               moving_piece, R, moving_piece, h1, from, f1, to,
                               color_flag);
      break;

    // white castles queen side
    case (c1):
      // move A rook
      accumulator_addaddsubsub(accumulator, prev_accumulator, white_king_square,
                               black_king_square, white_bucket, black_bucket, R,
                               moving_piece, R, moving_piece, a1, from, d1, to,
                               color_flag);
      break;

    // black castles king side
    case (g8):
      // move H rook
      accumulator_addaddsubsub(accumulator, prev_accumulator, white_king_square,
                               black_king_square, white_bucket, black_bucket, r,
                               moving_piece, r, moving_piece, h8, from, f8, to,
                               color_flag);
      break;

    // black castles queen side
    case (c8):
      // move A rook
      accumulator_addaddsubsub(accumulator, prev_accumulator, white_king_square,
                               black_king_square, white_bucket, black_bucket, r,
                               moving_piece, r, moving_piece, a8, from, d8, to,
                               color_flag);
      break;
    }
  } else {
    accumulator_addsub(accumulator, prev_accumulator, white_king_square,
                       black_king_square, white_bucket, black_bucket,
                       moving_piece, moving_piece, from, to, color_flag);
  }
}
