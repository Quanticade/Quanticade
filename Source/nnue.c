#include "nnue.h"
#include "bitboards.h"
#include "enums.h"
#include "incbin/incbin.h"
#include "move.h"
#include "simd.h"
#include "structs.h"
#include "uci.h"
#include "utils.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

nnue_t nnue;

struct raw_net {
  float feature_weights[KING_BUCKETS][INPUT_WEIGHTS][L1_SIZE];
  float feature_bias[L1_SIZE];
  float l1_weights[OUTPUT_BUCKETS][L2_SIZE][L1_SIZE];
  float l1_bias[OUTPUT_BUCKETS][L2_SIZE];
  float l2_weights[OUTPUT_BUCKETS][L3_SIZE][L2_SIZE];
  float l2_bias[OUTPUT_BUCKETS][L3_SIZE];
  float l3_weights[OUTPUT_BUCKETS][L3_SIZE];
  float l3_bias[OUTPUT_BUCKETS];
};

struct raw_net net;

const int INT8_PER_INT32 = sizeof(int) / sizeof(int8_t);

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

static inline uint8_t get_king_bucket(uint8_t side, uint8_t square) {
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

static float clamp_float(float d, float min, float max) {
  const float t = d < min ? min : d;
  return t > max ? max : t;
}

static inline float screlu_float(float value) {
  const float clipped = clamp_float(value, 0.0f, 1.0f);
  return clipped * clipped;
}

#ifndef USE_SIMD

static int32_t clamp_int32(int32_t d, int32_t min, int32_t max) {
  const int32_t t = d < min ? min : d;
  return t > max ? max : t;
}

static inline int32_t screlu(int16_t value) {
  const int32_t clipped = clamp_int32((int32_t)value, 0, INPUT_QUANT);
  return clipped * clipped;
}

static inline int16_t crelu(int16_t value) {
  return clamp_int32((int32_t)value, 0, INPUT_QUANT);
}

static inline float crelu_float(float value) {
  return clamp_float(value, 0.0f, 1.0f);
}

#endif

static inline uint8_t calculate_output_bucket(position_t *pos) {
  uint8_t pieces = popcount(pos->occupancies[2]);
  return (pieces - 2) / 4;
}

static inline void transpose(void) {
#if defined(USE_SIMD)
  for (int b = 0; b < OUTPUT_BUCKETS; b++) {
    for (int l1 = 0; l1 < L1_SIZE / INT8_PER_INT32; l1++) {
      for (int l2 = 0; l2 < L2_SIZE; l2++) {
        for (int c = 0; c < INT8_PER_INT32; c++) {
          nnue.l1_weights[b][l1 * INT8_PER_INT32 * L2_SIZE +
                             l2 * INT8_PER_INT32 + c] =
              round(net.l1_weights[b][l2][l1 * INT8_PER_INT32 + c] * L1_QUANT);
        }
      }
    }
  }
#else
  for (int b = 0; b < OUTPUT_BUCKETS; b++) {
    for (int l1 = 0; l1 < L1_SIZE; l1++) {
      for (int l2 = 0; l2 < L2_SIZE; l2++) {
        nnue.l1_weights[b][l1 * L2_SIZE + l2] =
            round(net.l1_weights[b][l2][l1] * L1_QUANT);
      }
    }
  }
#endif
  for (int b = 0; b < KING_BUCKETS; b++) {
    for (int input = 0; input < INPUT_WEIGHTS; input++) {
      for (int l1 = 0; l1 < L1_SIZE; l1++) {
        nnue.feature_weights[b][input][l1] =
            round(net.feature_weights[b][input][l1] * INPUT_QUANT);
      }
    }
  }
  for (int l1 = 0; l1 < L1_SIZE; l1++) {
    nnue.feature_bias[l1] = round(net.feature_bias[l1] * INPUT_QUANT);
  }
  for (int b = 0; b < OUTPUT_BUCKETS; b++) {
    for (int l2 = 0; l2 < L2_SIZE; l2++) {
      for (int l3 = 0; l3 < L3_SIZE; l3++) {
        nnue.l2_weights[b][l2][l3] = net.l2_weights[b][l3][l2];
      }
    }
  }
  for (int b = 0; b < OUTPUT_BUCKETS; b++) {
    for (int l3 = 0; l3 < L3_SIZE; l3++) {
      nnue.l3_weights[b][l3] = net.l3_weights[b][l3];
    }
  }
  memcpy(nnue.l1_bias, net.l1_bias, sizeof(net.l1_bias));
  memcpy(nnue.l2_bias, net.l2_bias, sizeof(net.l2_bias));
  memcpy(nnue.l3_bias, net.l3_bias, sizeof(net.l3_bias));
}

int nnue_init_incbin(void) {
  uint64_t memoryIndex = 0;
  memcpy(net.feature_weights, &gEVALData[memoryIndex],
         sizeof(net.feature_weights));
  memoryIndex += sizeof(net.feature_weights);
  memcpy(net.feature_bias, &gEVALData[memoryIndex], sizeof(net.feature_bias));
  memoryIndex += sizeof(net.feature_bias);

  memcpy(&net.l1_weights, &gEVALData[memoryIndex], sizeof(net.l1_weights));
  memoryIndex += sizeof(net.l1_weights);
  memcpy(&net.l1_bias, &gEVALData[memoryIndex], sizeof(net.l1_bias));
  memoryIndex += sizeof(net.l1_bias);

  memcpy(&net.l2_weights, &gEVALData[memoryIndex], sizeof(net.l2_weights));
  memoryIndex += sizeof(net.l2_weights);
  memcpy(&net.l2_bias, &gEVALData[memoryIndex], sizeof(net.l2_bias));
  memoryIndex += sizeof(net.l2_bias);

  memcpy(&net.l3_weights, &gEVALData[memoryIndex], sizeof(net.l3_weights));
  memoryIndex += sizeof(net.l3_weights);
  memcpy(&net.l3_bias, &gEVALData[memoryIndex], sizeof(net.l3_bias));
  return 1;
}

void nnue_init(const char *nnue_file_name) {
  // open the nn file
  FILE *nn = fopen(nnue_file_name, "rb");

  // if it's not invalid read the config values from it
  if (nn) {
    // initialize an accumulator for every input of the second layer
    size_t read = 0;
    size_t objectsExpected = 0;
    objectsExpected += sizeof(net.feature_weights) / sizeof(float);
    objectsExpected += sizeof(net.feature_bias) / sizeof(float);
    objectsExpected += sizeof(net.l1_weights) / sizeof(float);
    objectsExpected += sizeof(net.l1_bias) / sizeof(float);
    objectsExpected += sizeof(net.l2_weights) / sizeof(float);
    objectsExpected += sizeof(net.l2_bias) / sizeof(float);
    objectsExpected += sizeof(net.l3_weights) / sizeof(float);
    objectsExpected += sizeof(net.l3_bias) / sizeof(float);

    read += fread(net.feature_weights, sizeof(float),
                  sizeof(net.feature_weights) / sizeof(float), nn);
    read += fread(net.feature_bias, sizeof(float),
                  sizeof(net.feature_bias) / sizeof(float), nn);
    read += fread(net.l1_weights, sizeof(float),
                  sizeof(net.l1_weights) / sizeof(float), nn);
    read += fread(&net.l1_bias, sizeof(float),
                  sizeof(net.l1_bias) / sizeof(float), nn);
    read += fread(net.l2_weights, sizeof(float),
                  sizeof(net.l2_weights) / sizeof(float), nn);
    read += fread(&net.l2_bias, sizeof(float),
                  sizeof(net.l2_bias) / sizeof(float), nn);
    read += fread(net.l3_weights, sizeof(float),
                  sizeof(net.l3_weights) / sizeof(float), nn);
    read += fread(&net.l3_bias, sizeof(float),
                  sizeof(net.l3_bias) / sizeof(float), nn);

    if (read != objectsExpected) {
      printf("We read: %zu but the expected is %zu\n", read, objectsExpected);
      printf("Error loading the net, aborting\n");
      exit(1);
    }

    // after reading the config we can close the file
    fclose(nn);
  } else {
    printf("NNUE file not found. Loading from incbin\n");
    if (nnue_init_incbin() == 0) {
      printf("Failed to load network from incbin. Exiting\n");
      exit(1);
    }
  }
  transpose();
}

static inline int16_t get_idx(uint8_t side, uint8_t piece, uint8_t square,
                              uint8_t king_square, uint8_t force_hm,
                              uint8_t mirror) {
  const size_t COLOR_STRIDE = 64 * 6;
  const size_t PIECE_STRIDE = 64;

  uint8_t do_mirror = force_hm ? mirror : ((king_square & 7) >= 4);
  if (do_mirror) {
    square ^= 7;
  }

  int piece_type = piece > 5 ? piece - 6 : piece;
  int color = piece / 6;

  int16_t idx =
      side == white
          ? color * COLOR_STRIDE + piece_type * PIECE_STRIDE + (square ^ 56)
          : (1 ^ color) * COLOR_STRIDE + piece_type * PIECE_STRIDE + square;

  return idx;
}

static inline void refresh_accumulator(thread_t *thread, position_t *pos,
                                       accumulator_t *accumulator) {
  uint8_t side = pos->side ^ 1;
  uint8_t king_square = get_lsb(pos->bitboards[side == white ? K : k]);
  uint8_t bucket = get_king_bucket(side, king_square);
  uint8_t do_hm = (king_square & 7) >= 4;
  accumulator_t *finny_accumulator =
      &thread->finny_tables[do_hm][bucket].accumulators;
  uint64_t *finny_bitboards =
      thread->finny_tables[do_hm][bucket].bitboards[side];

  for (uint8_t piece = P; piece <= k; ++piece) {
    uint64_t added = pos->bitboards[piece] & ~finny_bitboards[piece];
    uint64_t removed = finny_bitboards[piece] & ~pos->bitboards[piece];

    while (added && removed) {
      uint8_t added_square = get_lsb(added);
      pop_bit(added, added_square);
      uint8_t removed_square = get_lsb(removed);
      pop_bit(removed, removed_square);
      size_t added_index =
          get_idx(side, piece, added_square, king_square, 0, 0);
      size_t removed_index =
          get_idx(side, piece, removed_square, king_square, 0, 0);

      for (int i = 0; i < L1_SIZE; ++i)
        finny_accumulator->accumulator[side][i] +=
            nnue.feature_weights[bucket][added_index][i] -
            nnue.feature_weights[bucket][removed_index][i];
    }

    while (added) {
      uint8_t square = get_lsb(added);
      pop_bit(added, square);
      size_t index = get_idx(side, piece, square, king_square, 0, 0);

      for (int i = 0; i < L1_SIZE; ++i)
        finny_accumulator->accumulator[side][i] +=
            nnue.feature_weights[bucket][index][i];
    }

    while (removed) {
      uint8_t square = get_lsb(removed);
      pop_bit(removed, square);
      size_t index = get_idx(side, piece, square, king_square, 0, 0);

      for (int i = 0; i < L1_SIZE; ++i)
        finny_accumulator->accumulator[side][i] -=
            nnue.feature_weights[bucket][index][i];
    }
  }
  memcpy(accumulator->accumulator[side], finny_accumulator->accumulator[side],
         L1_SIZE * sizeof(int16_t));
  memcpy(finny_bitboards, pos->bitboards, 12 * sizeof(uint64_t));
}

void init_accumulator(position_t *pos, accumulator_t *accumulator) {
  uint8_t white_bucket = get_king_bucket(white, get_lsb(pos->bitboards[K]));
  uint8_t black_bucket = get_king_bucket(black, get_lsb(pos->bitboards[k]));
  for (int i = 0; i < L1_SIZE; ++i) {
    accumulator->accumulator[0][i] = nnue.feature_bias[i];
    accumulator->accumulator[1][i] = nnue.feature_bias[i];
  }

  for (int piece = P; piece <= k; ++piece) {
    uint64_t bitboard = pos->bitboards[piece];
    while (bitboard) {
      int square = get_lsb(bitboard);
      size_t white_idx =
          get_idx(white, piece, square, get_lsb(pos->bitboards[K]), 0, 0);
      size_t black_idx =
          get_idx(black, piece, square, get_lsb(pos->bitboards[k]), 0, 0);

      // updates all the pieces in the accumulators
      for (int i = 0; i < L1_SIZE; ++i)
        accumulator->accumulator[white][i] +=
            nnue.feature_weights[white_bucket][white_idx][i];

      for (int i = 0; i < L1_SIZE; ++i)
        accumulator->accumulator[black][i] +=
            nnue.feature_weights[black_bucket][black_idx][i];

      pop_bit(bitboard, square);
    }
  }
}

void init_accumulator_bucket(position_t *pos, accumulator_t *accumulator,
                             uint8_t bucket, uint8_t do_hm) {
  for (int i = 0; i < L1_SIZE; ++i) {
    accumulator->accumulator[0][i] = nnue.feature_bias[i];
    accumulator->accumulator[1][i] = nnue.feature_bias[i];
  }

  for (int piece = P; piece <= k; ++piece) {
    uint64_t bitboard = pos->bitboards[piece];
    while (bitboard) {
      int square = get_lsb(bitboard);
      size_t white_idx = get_idx(white, piece, square, 0, 1, do_hm);
      size_t black_idx = get_idx(black, piece, square, 0, 1, do_hm);

      // updates all the pieces in the accumulators
      for (int i = 0; i < L1_SIZE; ++i)
        accumulator->accumulator[white][i] +=
            nnue.feature_weights[bucket][white_idx][i];

      for (int i = 0; i < L1_SIZE; ++i)
        accumulator->accumulator[black][i] +=
            nnue.feature_weights[bucket][black_idx][i];

      pop_bit(bitboard, square);
    }
  }
}

void init_finny_tables(thread_t *thread, position_t *pos) {
  for (uint8_t do_hm = 0; do_hm < 2; ++do_hm) {
    for (uint8_t bucket = 0; bucket < KING_BUCKETS; ++bucket) {
      init_accumulator_bucket(pos,
                              &thread->finny_tables[do_hm][bucket].accumulators,
                              bucket, do_hm);
      memcpy(thread->finny_tables[do_hm][bucket].bitboards[white],
             pos->bitboards, 12 * sizeof(uint64_t));
      memcpy(thread->finny_tables[do_hm][bucket].bitboards[black],
             pos->bitboards, 12 * sizeof(uint64_t));
    }
  }
}

int nnue_eval_pos(position_t *pos, accumulator_t *accumulator) {
  uint8_t white_bucket = get_king_bucket(white, get_lsb(pos->bitboards[K]));
  uint8_t black_bucket = get_king_bucket(black, get_lsb(pos->bitboards[k]));
  for (int i = 0; i < L1_SIZE; ++i) {
    accumulator->accumulator[0][i] = nnue.feature_bias[i];
    accumulator->accumulator[1][i] = nnue.feature_bias[i];
  }

  uint8_t out_bucket = calculate_output_bucket(pos);

  for (int piece = P; piece <= k; ++piece) {
    uint64_t bitboard = pos->bitboards[piece];
    while (bitboard) {
      int square = get_lsb(bitboard);
      int16_t white_idx =
          get_idx(white, piece, square, get_lsb(pos->bitboards[K]), 0, 0);
      int16_t black_idx =
          get_idx(black, piece, square, get_lsb(pos->bitboards[k]), 0, 0);

      // updates all the pieces in the accumulators
      for (int i = 0; i < L1_SIZE; ++i)
        accumulator->accumulator[white][i] +=
            nnue.feature_weights[white_bucket][white_idx][i];

      for (int i = 0; i < L1_SIZE; ++i)
        accumulator->accumulator[black][i] +=
            nnue.feature_weights[black_bucket][black_idx][i];

      pop_bit(bitboard, square);
    }
  }

  int16_t *stmAcc = accumulator->accumulator[pos->side];
  int16_t *oppAcc = accumulator->accumulator[1 - pos->side];

  int8_t l1Neurons[L1_SIZE];
  for (int l1 = 0; l1 < L1_SIZE / 2; l1++) {
    int16_t stmClipped1 = clamp((int)(stmAcc[l1]), 0, INPUT_QUANT);
    int16_t stmClipped2 =
        clamp((int)(stmAcc[l1 + L1_SIZE / 2]), 0, INPUT_QUANT);
    l1Neurons[l1] = (stmClipped1 * stmClipped2) >> INPUT_SHIFT;

    int16_t oppClipped1 = clamp((int)(oppAcc[l1]), 0, INPUT_QUANT);
    int16_t oppClipped2 =
        clamp((int)(oppAcc[l1 + L1_SIZE / 2]), 0, INPUT_QUANT);
    l1Neurons[l1 + L1_SIZE / 2] = (oppClipped1 * oppClipped2) >> INPUT_SHIFT;
  }

  int l2Neurons[L2_SIZE] = {0};

  for (int l1 = 0; l1 < L1_SIZE; l1++) {
    for (int l2 = 0; l2 < L2_SIZE; l2++) {
      l2Neurons[l2] +=
          l1Neurons[l1] * nnue.l1_weights[out_bucket][l1 * L2_SIZE + l2];
    }
  }

  float l3Neurons[L3_SIZE];
  memcpy(l3Neurons, nnue.l2_bias[out_bucket], sizeof(l3Neurons));

  const float L1_NORMALISATION =
      (float)(1 << INPUT_SHIFT) / (float)(INPUT_QUANT * INPUT_QUANT * L1_QUANT);

  for (int l2 = 0; l2 < L2_SIZE; l2++) {
    float l2Result = (float)(l2Neurons[l2]) * L1_NORMALISATION +
                     (nnue.l1_bias[out_bucket][l2]);
    float l2Activated = screlu_float(l2Result);

    for (int l3 = 0; l3 < L3_SIZE; l3++) {
      l3Neurons[l3] += l2Activated * nnue.l2_weights[out_bucket][l2][l3];
    }
  }

  float result = nnue.l3_bias[out_bucket];
  for (int l3 = 0; l3 < L3_SIZE; l3++) {
    float l3Activated = screlu_float(l3Neurons[l3]);
    result += l3Activated * nnue.l3_weights[out_bucket][l3];
  }
  // TODO reduce add

  return (int16_t)(result * SCALE);
}

int nnue_evaluate(thread_t *thread, position_t *pos, accumulator_t *accumulator) {
  const uint8_t out_bucket = calculate_output_bucket(pos);
  const int16_t *stmAcc = accumulator->accumulator[pos->side];
  const int16_t *oppAcc = accumulator->accumulator[1 - pos->side];

  simd_t *layers = &thread->neurons;

  memset(layers->l2_neurons, 0, sizeof(layers->l2_neurons));

  const float L1_NORMALISATION =
      (float)(1 << INPUT_SHIFT) / (float)(INPUT_QUANT * INPUT_QUANT * L1_QUANT);

#if defined(USE_SIMD)
  const int FLOAT_VEC_SIZE = sizeof(veci_t) / sizeof(float);
  const int I16_VEC_SIZE = sizeof(veci_t) / sizeof(int16_t);

  veci_t i16_zero = zero();
  veci_t i16_quant = set_epi16((int16_t)INPUT_QUANT);

  for (int l1 = 0; l1 < L1_SIZE / 2; l1 += 2 * I16_VEC_SIZE) {
    // STM
    veci_t clipped1 = clip_epi16(*((veci_t *)&stmAcc[l1]), i16_zero, i16_quant);
    veci_t clipped2 =
        min_epi16(*((veci_t *)&stmAcc[l1 + L1_SIZE / 2]), i16_quant);
    veci_t shift = slli_epi16(clipped1, 16 - INPUT_SHIFT);
    veci_t mul1 = mulhi_epi16(shift, clipped2);

    clipped1 = clip_epi16(*((veci_t *)&stmAcc[l1 + I16_VEC_SIZE]), i16_zero,
                          i16_quant);
    clipped2 = min_epi16(*((veci_t *)&stmAcc[l1 + I16_VEC_SIZE + L1_SIZE / 2]),
                         i16_quant);
    shift = slli_epi16(clipped1, 16 - INPUT_SHIFT);
    veci_t mul2 = mulhi_epi16(shift, clipped2);

    veci_t u8s = packus_epi16(mul1, mul2);
    vec_store_i((veci_t *)&layers->l1_neurons[l1], u8s);

    // NSTM
    clipped1 = clip_epi16(*((veci_t *)&oppAcc[l1]), i16_zero, i16_quant);
    clipped2 = min_epi16(*((veci_t *)&oppAcc[l1 + L1_SIZE / 2]), i16_quant);
    shift = slli_epi16(clipped1, 16 - INPUT_SHIFT);
    mul1 = mulhi_epi16(shift, clipped2);

    clipped1 = clip_epi16(*((veci_t *)&oppAcc[l1 + I16_VEC_SIZE]), i16_zero,
                          i16_quant);
    clipped2 = min_epi16(*((veci_t *)&oppAcc[l1 + I16_VEC_SIZE + L1_SIZE / 2]),
                         i16_quant);
    shift = slli_epi16(clipped1, 16 - INPUT_SHIFT);
    mul2 = mulhi_epi16(shift, clipped2);

    u8s = packus_epi16(mul1, mul2);
    vec_store_i((veci_t *)&layers->l1_neurons[l1 + L1_SIZE / 2], u8s);
  }

  int *l1Packs = (int *)layers->l1_neurons;

  for (int l1 = 0; l1 < L1_SIZE; l1 += INT8_PER_INT32) {
    for (int l2 = 0; l2 < L2_SIZE; l2 += sizeof(veci_t) / sizeof(int32_t)) {
      veci_t u8 = set_epi32(l1Packs[l1 / INT8_PER_INT32]);
      veci_t i8 =
          *((veci_t *)&nnue
                .l1_weights[out_bucket][l1 * L2_SIZE + INT8_PER_INT32 * l2]);
      *((veci_t *)&layers->l2_neurons[l2]) =
          dpbusd_epi32(*((veci_t *)&layers->l2_neurons[l2]), u8, i8);
    }
  }

  memcpy(layers->l3_neurons, nnue.l2_bias[out_bucket], sizeof(layers->l3_neurons));

  vecf_t norm_ps = set_ps1(L1_NORMALISATION);
  vecf_t one_ps = set_ps1(1.0f);
  vecf_t zero_ps = set_ps1(0.0f);

  for (int l2 = 0; l2 < L2_SIZE / FLOAT_VEC_SIZE; l2++) {
    vecf_t converted =
        cvtepi32_ps(*((veci_t *)&layers->l2_neurons[l2 * FLOAT_VEC_SIZE]));
    vecf_t l2_result =
        add_ps(mul_ps(converted, norm_ps),
               *((vecf_t *)&nnue.l1_bias[out_bucket][l2 * FLOAT_VEC_SIZE]));
    vecf_t l2_clipped = clip_ps(l2_result, one_ps, zero_ps);
    *((vecf_t *)&layers->l2_floats[l2 * FLOAT_VEC_SIZE]) =
        mul_ps(l2_clipped, l2_clipped);
  }

  for (int l2 = 0; l2 < L2_SIZE; l2++) {
    for (int l3 = 0; l3 < L3_SIZE / FLOAT_VEC_SIZE; l3++) {
      *((vecf_t *)&layers->l3_neurons[l3 * FLOAT_VEC_SIZE]) = fmadd_ps(
          set_ps1(layers->l2_floats[l2]),
          *((vecf_t *)&nnue.l2_weights[out_bucket][l2][l3 * FLOAT_VEC_SIZE]),
          *((vecf_t *)&layers->l3_neurons[l3 * FLOAT_VEC_SIZE]));
    }
  }

  const uint8_t chunks = 64 / sizeof(vecf_t);

  vecf_t result_sums[chunks];
  for (int i = 0; i < chunks; i++) {
    result_sums[i] = zero_ps;
  }

  for (int l3 = 0; l3 < L3_SIZE / FLOAT_VEC_SIZE; l3 += chunks) {
    for (int chunk = 0; chunk < chunks; chunk++) {
      vecf_t l3_clipped =
          clip_ps(*((vecf_t *)&layers->l3_neurons[(l3 + chunk) * FLOAT_VEC_SIZE]),
                  one_ps, zero_ps);
      vecf_t l3_activated = mul_ps(l3_clipped, l3_clipped);
      result_sums[chunk] = fmadd_ps(
          l3_activated,
          *((vecf_t *)&nnue
                .l3_weights[out_bucket][(l3 + chunk) * FLOAT_VEC_SIZE]),
          result_sums[chunk]);
    }
  }

  float result = nnue.l3_bias[out_bucket] + reduce_add_ps(result_sums);
#else

  for (int l1 = 0; l1 < L1_SIZE / 2; l1++) {
    int16_t stmClipped1 = clamp((int)(stmAcc[l1]), 0, INPUT_QUANT);
    int16_t stmClipped2 =
        clamp((int)(stmAcc[l1 + L1_SIZE / 2]), 0, INPUT_QUANT);
    layers->l1_neurons[l1] = (stmClipped1 * stmClipped2) >> INPUT_SHIFT;

    int16_t oppClipped1 = clamp((int)(oppAcc[l1]), 0, INPUT_QUANT);
    int16_t oppClipped2 =
        clamp((int)(oppAcc[l1 + L1_SIZE / 2]), 0, INPUT_QUANT);
    layers->l1_neurons[l1 + L1_SIZE / 2] = (oppClipped1 * oppClipped2) >> INPUT_SHIFT;
  }

  for (int l1 = 0; l1 < L1_SIZE; l1++) {
    for (int l2 = 0; l2 < L2_SIZE; l2++) {
      layers->l2_neurons[l2] +=
          layers->l1_neurons[l1] * nnue.l1_weights[out_bucket][l1 * L2_SIZE + l2];
    }
  }

  memcpy(layers->l3_neurons, nnue.l2_bias[out_bucket], sizeof(layers->l3_neurons));

  for (int l2 = 0; l2 < L2_SIZE; l2++) {
    float l2Result = (float)(layers->l2_neurons[l2]) * L1_NORMALISATION +
                     (nnue.l1_bias[out_bucket][l2]);
    float l2Activated = crelu_float(l2Result);
    l2Activated *= l2Activated;

    for (int l3 = 0; l3 < L3_SIZE; l3++) {
      layers->l3_neurons[l3] += l2Activated * nnue.l2_weights[out_bucket][l2][l3];
    }
  }

  float result = nnue.l3_bias[out_bucket];
  for (int l3 = 0; l3 < L3_SIZE; l3++) {
    float l3Activated = crelu_float(layers->l3_neurons[l3]);
    l3Activated *= l3Activated;
    result += l3Activated * nnue.l3_weights[out_bucket][l3];
  }

#endif
  return (int16_t)(result * SCALE);
}

static inline void
accumulator_addsub(accumulator_t *accumulator, accumulator_t *prev_accumulator,
                   uint8_t white_king_square, uint8_t black_king_square,
                   uint8_t white_bucket, uint8_t black_bucket, uint8_t piece1,
                   uint8_t piece2, uint8_t from1, uint8_t to2,
                   uint8_t color_flag) {
  size_t white_idx_from =
      get_idx(white, piece1, from1, white_king_square, 0, 0);
  size_t black_idx_from =
      get_idx(black, piece1, from1, black_king_square, 0, 0);
  size_t white_idx_to = get_idx(white, piece2, to2, white_king_square, 0, 0);
  size_t black_idx_to = get_idx(black, piece2, to2, black_king_square, 0, 0);

  for (int i = 0; i < L1_SIZE; ++i) {
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
  size_t white_idx_from1 =
      get_idx(white, piece1, from1, white_king_square, 0, 0);
  size_t black_idx_from1 =
      get_idx(black, piece1, from1, black_king_square, 0, 0);
  size_t white_idx_from2 =
      get_idx(white, piece2, from2, white_king_square, 0, 0);
  size_t black_idx_from2 =
      get_idx(black, piece2, from2, black_king_square, 0, 0);
  size_t white_idx_to = get_idx(white, piece3, to3, white_king_square, 0, 0);
  size_t black_idx_to = get_idx(black, piece3, to3, black_king_square, 0, 0);

  for (int i = 0; i < L1_SIZE; ++i) {
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
  size_t white_idx_from1 =
      get_idx(white, piece1, from1, white_king_square, 0, 0);
  size_t black_idx_from1 =
      get_idx(black, piece1, from1, black_king_square, 0, 0);
  size_t white_idx_from2 =
      get_idx(white, piece2, from2, white_king_square, 0, 0);
  size_t black_idx_from2 =
      get_idx(black, piece2, from2, black_king_square, 0, 0);
  size_t white_idx_to1 = get_idx(white, piece3, to3, white_king_square, 0, 0);
  size_t black_idx_to1 = get_idx(black, piece3, to3, black_king_square, 0, 0);
  size_t white_idx_to2 = get_idx(white, piece4, to4, white_king_square, 0, 0);
  size_t black_idx_to2 = get_idx(black, piece4, to4, black_king_square, 0, 0);

  for (int i = 0; i < L1_SIZE; ++i) {
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

static inline void
accumulator_make_move(accumulator_t *accumulator,
                      accumulator_t *prev_accumulator,
                      uint8_t white_king_square, uint8_t black_king_square,
                      uint8_t white_bucket, uint8_t black_bucket, uint8_t side,
                      int move, uint8_t *mailbox, uint8_t color_flag) {
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

void update_nnue(position_t *pos, thread_t *thread, uint8_t mailbox_copy[64],
                 uint16_t move) {
  uint8_t white_king_square = get_lsb(pos->bitboards[K]);
  uint8_t black_king_square = get_lsb(pos->bitboards[k]);
  uint8_t white_bucket = get_king_bucket(white, white_king_square);
  uint8_t black_bucket = get_king_bucket(black, black_king_square);
  if (need_refresh(mailbox_copy, move)) {
    if (pos->side == black) {
      refresh_accumulator(thread, pos, &thread->accumulator[pos->ply]);
      accumulator_make_move(&thread->accumulator[pos->ply],
                            &thread->accumulator[pos->ply - 1],
                            white_king_square, black_king_square, white_bucket,
                            black_bucket, pos->side, move, mailbox_copy, black);
    } else if (pos->side == white) {
      refresh_accumulator(thread, pos, &thread->accumulator[pos->ply]);
      accumulator_make_move(&thread->accumulator[pos->ply],
                            &thread->accumulator[pos->ply - 1],
                            white_king_square, black_king_square, white_bucket,
                            black_bucket, pos->side, move, mailbox_copy, white);
    }
  } else {
    accumulator_make_move(&thread->accumulator[pos->ply],
                          &thread->accumulator[pos->ply - 1], white_king_square,
                          black_king_square, white_bucket, black_bucket,
                          pos->side, move, mailbox_copy, both);
  }
}
