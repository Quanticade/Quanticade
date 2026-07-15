#include "nnue.h"
#include "arch.h"
#include "attacks.h"
#include "bitboards.h"
#include "enums.h"
#include "incbin/incbin.h"
#include "move.h"
#include "simd.h"
#include "structs.h"
#include "utils.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int EVAL_SCALE = 298;

const nnue_t *nnue;

const int INT8_PER_INT32 = sizeof(int) / sizeof(int8_t);

static int feature_base_lut[12][12][2];
static uint8_t precomputed_piece_index[12][64][64];

static int PIECE_TARGET_COUNT[6] = {6, 10, 8, 8, 10, 0};
static int PIECE_TARGET_MAP[6][6] = {
    {-1, 0, -1, 1, -1, -1}, {0, 1, 2, 3, 4, -1}, {0, 1, 2, 3, -1, -1},
    {0, 1, 2, 3, -1, -1},  {0, 1, 2, 3, 4, -1}, {-1, -1, -1, -1, -1, -1}};
static const uint8_t swap_color_pc[2][12] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
    {6, 7, 8, 9, 10, 11, 0, 1, 2, 3, 4, 5}};

#define FILE_A 0x0101010101010101ULL
#define FILE_B 0x0202020202020202ULL
#define FILE_C 0x0404040404040404ULL
#define FILE_D 0x0808080808080808ULL
#define FILE_E 0x1010101010101010ULL
#define FILE_F 0x2020202020202020ULL
#define FILE_G 0x4040404040404040ULL
#define FILE_H 0x8080808080808080ULL

static const uint64_t PAWN_PAWN_MASKS[8] = {
    FILE_A | FILE_B,
    FILE_A | FILE_B | FILE_C,
    FILE_B | FILE_C | FILE_D,
    FILE_C | FILE_D | FILE_E,
    FILE_D | FILE_E | FILE_F,
    FILE_E | FILE_F | FILE_G,
    FILE_F | FILE_G | FILE_H,
    FILE_G | FILE_H,
};

static int threat_offsets[12];
static int threat_piece_offsets[12];
static int threat_sq_offsets[12][64];
static uint64_t threat_attacks[12][64];

#if defined(USE_SIMD) && !defined(USE_AVX512ICL)
static uint16_t NNZ_TABLE[256][8];

static void init_nnz_table(void) {
  for (int i = 0; i < 256; i++) {
    int count = 0;
    for (int j = 0; j < 8; j++) {
      if (i & (1 << j))
        NNZ_TABLE[i][count++] = (uint16_t)j;
    }
  }
}
#endif

const uint8_t buckets[64] = {14, 14, 15, 15, 15, 15, 14, 14, 14, 14, 15, 15, 15,
                             15, 14, 14, 12, 12, 13, 13, 13, 13, 12, 12, 12, 12,
                             13, 13, 13, 13, 12, 12, 8,  9,  10, 11, 11, 10, 9,
                             8,  8,  9,  10, 11, 11, 10, 9,  8,  4,  5,  6,  7,
                             7,  6,  5,  4,  0,  1,  2,  3,  3,  2,  1,  0};

#if !defined(_MSC_VER)
INCBIN(EVAL, EVALFILE);
#else
const unsigned char gEVALData[1] = {};
const unsigned char *const gEVALEnd = &gEVALData[1];
const unsigned int gEVALSize = 1;
#endif

#if defined(__AVX512F__) || defined(USE_AVX512)
#define VECTOR_BYTES 64
#define REGISTERS 32
#elif defined(__AVX2__) || defined(USE_AVX2)
#define VECTOR_BYTES 32
#define REGISTERS 16
#elif defined(__ARM_NEON) || defined(USE_NEON) || defined(__aarch64__)
// aarch64 has 32 registers
#define VECTOR_BYTES 16
#define REGISTERS 32
#else
#define VECTOR_BYTES 16
#define REGISTERS 8
#endif

#define VEC_ELTS(T) ((int)(VECTOR_BYTES / sizeof(T)))

#define CHUNK_ELTS (VEC_ELTS(int16_t))
#define L1_VECTORS (L1_SIZE / CHUNK_ELTS)
#define CHUNK_SIZE (REGISTERS > (L1_VECTORS / 2) ? (L1_VECTORS / 2) : REGISTERS)

typedef int16_t vec_s16
    __attribute__((__vector_size__(CHUNK_ELTS * sizeof(int16_t))));
typedef int8_t vec_s8
    __attribute__((__vector_size__(CHUNK_ELTS * sizeof(int8_t))));

const uint8_t BUCKET_DIVISOR = (32 + OUTPUT_BUCKETS - 1) / OUTPUT_BUCKETS;

static inline uint8_t get_king_bucket(uint8_t side, uint8_t square) {
  return buckets[side ? square ^ 56 : square];
}

static float clamp_float(float d, float min, float max) {
  const float t = d < min ? min : d;
  return t > max ? max : t;
}

static inline float screlu_float(float value) {
  const float clipped = clamp_float(value, 0.0f, 1.0f);
  return clipped * clipped;
}

static inline float crelu_float(float value) {
  return clamp_float(value, 0.0f, 1.0f);
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
#endif

static inline uint8_t calculate_output_bucket(position_t *pos) {
  const uint8_t pieces = popcount(pos->occupancies[2]);
  return (pieces - 2) / 4;
}

static void init_threat_tables(void) {
  int current_offset = 0;
  for (int c = 0; c < 2; c++) {
    for (int pt = 0; pt < 6; pt++) {
      int pc = pt + c * 6;
      int current_piece_offset = 0;
      for (int sq = 0; sq < 64; sq++) {
        threat_sq_offsets[pc][sq] = current_piece_offset;
        uint64_t att = 0;
        int engine_sq = sq ^ 56;

        if (pt == 0) {
          if (sq >= 8 && sq <= 55)
            att = __builtin_bswap64(get_pawn_attacks(c, engine_sq));
        } else if (pt == 1) {
          att = __builtin_bswap64(get_knight_attacks(engine_sq));
        } else if (pt == 2) {
          att = __builtin_bswap64(get_bishop_attacks(engine_sq, 0));
        } else if (pt == 3) {
          att = __builtin_bswap64(get_rook_attacks(engine_sq, 0));
        } else if (pt == 4) {
          att = __builtin_bswap64(get_queen_attacks(engine_sq, 0));
        }

        threat_attacks[pc][sq] = att;
        current_piece_offset += __builtin_popcountll(att);
      }
      threat_piece_offsets[pc] = current_piece_offset;
      threat_offsets[pc] = current_offset;
      current_offset += PIECE_TARGET_COUNT[pt] * current_piece_offset;
    }
  }

  for (int a = 0; a < 12; a++) {
    for (int v = 0; v < 12; v++) {
      int a_pt = a % 6;
      int v_pt = v % 6;
      int a_c = a / 6;
      int v_c = v / 6;

      int map = PIECE_TARGET_MAP[a_pt][v_pt];
      int opposed = (a_c != v_c);
      int semi_excluded = (a_pt == v_pt) && (opposed || a_pt != 0);

      for (int src_less_dest = 0; src_less_dest < 2; src_less_dest++) {
        if (map == -1 || (semi_excluded && src_less_dest)) {
          // Flag as invalid by setting a massive negative number
          feature_base_lut[a][v][src_less_dest] = -100000;
        } else {
          int color_base = v_c * (PIECE_TARGET_COUNT[a_pt] / 2);
          int offset = color_base + map;
          feature_base_lut[a][v][src_less_dest] =
              threat_offsets[a] + (offset * threat_piece_offsets[a]);
        }
      }
    }
  }

  for (int pc = 0; pc < 12; pc++) {
    for (int src = 0; src < 64; src++) {
      uint64_t attacks = threat_attacks[pc][src];
      for (int dest = 0; dest < 64; dest++) {
        uint64_t mask = (1ULL << dest) - 1;
        precomputed_piece_index[pc][src][dest] =
            __builtin_popcountll(attacks & mask);
      }
    }
  }
}

static inline int get_threat_index(int perspective, int king_sq,
                                   int attacker_pc, int victim_pc, int src,
                                   int dest) {
  attacker_pc = swap_color_pc[perspective][attacker_pc];
  victim_pc = swap_color_pc[perspective][victim_pc];

  int flip = (perspective == white ? 56 : 0) ^ ((king_sq & 7) >= 4 ? 7 : 0);
  src ^= flip;
  dest ^= flip;

  int feature_base = feature_base_lut[attacker_pc][victim_pc][src < dest];
  int sq_offset = threat_sq_offsets[attacker_pc][src];

  int piece_index = precomputed_piece_index[attacker_pc][src][dest];

  return feature_base + sq_offset + piece_index;
}

void nnue_init(void) {
  nnue = (const nnue_t *)gEVALData;
  init_threat_tables();
#if defined(USE_SIMD) && !defined(USE_AVX512ICL)
  init_nnz_table();
#endif
}

static inline int16_t get_idx(uint8_t side, uint8_t piece, uint8_t square,
                              uint8_t king_square, uint8_t force_hm,
                              uint8_t mirror) {
  const size_t COLOR_STRIDE = 64 * 6;
  const size_t PIECE_STRIDE = 64;

  const uint8_t do_mirror = force_hm ? mirror : ((king_square & 7) >= 4);
  if (do_mirror) {
    square ^= 7;
  }

  const uint8_t piece_type = piece > 5 ? piece - 6 : piece;
  const uint8_t color = piece / 6;

  const int16_t idx =
      side == white
          ? color * COLOR_STRIDE + piece_type * PIECE_STRIDE + (square ^ 56)
          : (1 ^ color) * COLOR_STRIDE + piece_type * PIECE_STRIDE + square;

  return idx;
}

void rebuild_aux(position_t *pos, uint8_t *mailbox, accumulator_t *acc) {
  for (int i = 0; i < L1_SIZE; ++i) {
    acc->aux_accumulator[white][i] = 0;
    acc->aux_accumulator[black][i] = 0;
  }

  uint64_t occ = pos->occupancies[both];
  uint8_t white_king_sq = get_lsb(pos->bitboards[K]);
  uint8_t black_king_sq = get_lsb(pos->bitboards[k]);

  for (int c = 0; c < 2; ++c) {
    for (int pt = 0; pt < 5; ++pt) {
      int pc = pt + c * 6;
      uint64_t attackers = pos->bitboards[pc];
      while (attackers) {
        int src = poplsb(&attackers);
        uint64_t engine_attacks = 0;

        if (pt == 0) {
          if (src >= 8 && src <= 55)
            engine_attacks = get_pawn_attacks(c, src);
        } else if (pt == 1) {
          engine_attacks = get_knight_attacks(src);
        } else if (pt == 2) {
          engine_attacks = get_bishop_attacks(src, occ);
        } else if (pt == 3) {
          engine_attacks = get_rook_attacks(src, occ);
        } else if (pt == 4) {
          engine_attacks = get_queen_attacks(src, occ);
        }

        engine_attacks &= occ & ~(pos->bitboards[K] | pos->bitboards[k]);

        while (engine_attacks) {
          int dest = poplsb(&engine_attacks);
          int victim_pc = mailbox[dest];
          if (victim_pc > 11)
            continue;

          int w_idx =
              get_threat_index(white, white_king_sq, pc, victim_pc, src, dest);
          int b_idx =
              get_threat_index(black, black_king_sq, pc, victim_pc, src, dest);

          if (w_idx >= 0) {
            for (int i = 0; i < L1_SIZE; ++i) {
              acc->aux_accumulator[white][i] +=
                  nnue->feature_aux[w_idx][i];
            }
          }
          if (b_idx >= 0) {
            for (int i = 0; i < L1_SIZE; ++i) {
              acc->aux_accumulator[black][i] +=
                  nnue->feature_aux[b_idx][i];
            }
          }
        }
      }
    }
  }
}

static inline void refresh_accumulator(thread_t *thread, position_t *pos,
                                       accumulator_t *accumulator) {
  const uint8_t side = pos->side ^ 1;
  const uint8_t king_square = get_lsb(pos->bitboards[side == white ? K : k]);
  const uint8_t bucket = get_king_bucket(side, king_square);
  const uint8_t do_hm = (king_square & 7) >= 4;
  accumulator_t *finny_accumulator =
      &thread->finny_tables[do_hm][bucket].accumulators;
  uint64_t *finny_bitboards =
      thread->finny_tables[do_hm][bucket].bitboards[side];

  for (uint8_t piece = P; piece <= k; ++piece) {
    uint64_t added = pos->bitboards[piece] & ~finny_bitboards[piece];
    uint64_t removed = finny_bitboards[piece] & ~pos->bitboards[piece];

    while (added && removed) {
      const uint8_t added_square = get_lsb(added);
      pop_bit(added, added_square);
      const uint8_t removed_square = get_lsb(removed);
      pop_bit(removed, removed_square);
      const size_t added_index =
          get_idx(side, piece, added_square, king_square, 0, 0);
      const size_t removed_index =
          get_idx(side, piece, removed_square, king_square, 0, 0);

      for (int i = 0; i < L1_SIZE; ++i)
        finny_accumulator->psqt_accumulator[side][i] +=
            nnue->feature_weights[bucket][added_index][i] -
            nnue->feature_weights[bucket][removed_index][i];
    }

    while (added) {
      const uint8_t square = get_lsb(added);
      pop_bit(added, square);
      const size_t index = get_idx(side, piece, square, king_square, 0, 0);

      for (int i = 0; i < L1_SIZE; ++i)
        finny_accumulator->psqt_accumulator[side][i] +=
            nnue->feature_weights[bucket][index][i];
    }

    while (removed) {
      const uint8_t square = get_lsb(removed);
      pop_bit(removed, square);
      const size_t index = get_idx(side, piece, square, king_square, 0, 0);

      for (int i = 0; i < L1_SIZE; ++i)
        finny_accumulator->psqt_accumulator[side][i] -=
            nnue->feature_weights[bucket][index][i];
    }
  }
  memcpy(accumulator->psqt_accumulator[side],
         finny_accumulator->psqt_accumulator[side], L1_SIZE * sizeof(int16_t));
  memcpy(finny_bitboards, pos->bitboards, 12 * sizeof(uint64_t));

  rebuild_aux(pos, pos->mailbox, accumulator);
}

void init_accumulator(position_t *pos, accumulator_t *accumulator) {
  const uint8_t white_bucket =
      get_king_bucket(white, get_lsb(pos->bitboards[K]));
  const uint8_t black_bucket =
      get_king_bucket(black, get_lsb(pos->bitboards[k]));
  for (int i = 0; i < L1_SIZE; ++i) {
    accumulator->psqt_accumulator[0][i] = nnue->feature_bias[i];
    accumulator->psqt_accumulator[1][i] = nnue->feature_bias[i];
  }

  for (int piece = P; piece <= k; ++piece) {
    uint64_t bitboard = pos->bitboards[piece];
    while (bitboard) {
      const uint8_t square = get_lsb(bitboard);
      const size_t white_idx =
          get_idx(white, piece, square, get_lsb(pos->bitboards[K]), 0, 0);
      const size_t black_idx =
          get_idx(black, piece, square, get_lsb(pos->bitboards[k]), 0, 0);

      for (int i = 0; i < L1_SIZE; ++i)
        accumulator->psqt_accumulator[white][i] +=
            nnue->feature_weights[white_bucket][white_idx][i];

      for (int i = 0; i < L1_SIZE; ++i)
        accumulator->psqt_accumulator[black][i] +=
            nnue->feature_weights[black_bucket][black_idx][i];

      pop_bit(bitboard, square);
    }
  }
  rebuild_aux(pos, pos->mailbox, accumulator);
}

void init_accumulator_bucket(position_t *pos, accumulator_t *accumulator,
                             uint8_t bucket, uint8_t do_hm) {
  for (int i = 0; i < L1_SIZE; ++i) {
    accumulator->psqt_accumulator[0][i] = nnue->feature_bias[i];
    accumulator->psqt_accumulator[1][i] = nnue->feature_bias[i];
  }

  for (int piece = P; piece <= k; ++piece) {
    uint64_t bitboard = pos->bitboards[piece];
    while (bitboard) {
      const uint8_t square = get_lsb(bitboard);
      const size_t white_idx = get_idx(white, piece, square, 0, 1, do_hm);
      const size_t black_idx = get_idx(black, piece, square, 0, 1, do_hm);

      for (int i = 0; i < L1_SIZE; ++i)
        accumulator->psqt_accumulator[white][i] +=
            nnue->feature_weights[bucket][white_idx][i];

      for (int i = 0; i < L1_SIZE; ++i)
        accumulator->psqt_accumulator[black][i] +=
            nnue->feature_weights[bucket][black_idx][i];

      pop_bit(bitboard, square);
    }
  }
  rebuild_aux(pos, pos->mailbox, accumulator);
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
  const uint8_t white_bucket =
      get_king_bucket(white, get_lsb(pos->bitboards[K]));
  const uint8_t black_bucket =
      get_king_bucket(black, get_lsb(pos->bitboards[k]));
  for (int i = 0; i < L1_SIZE; ++i) {
    accumulator->psqt_accumulator[0][i] = nnue->feature_bias[i];
    accumulator->psqt_accumulator[1][i] = nnue->feature_bias[i];
  }

  const uint8_t out_bucket = calculate_output_bucket(pos);

  for (int piece = P; piece <= k; ++piece) {
    uint64_t bitboard = pos->bitboards[piece];
    while (bitboard) {
      const uint8_t square = get_lsb(bitboard);
      int16_t white_idx =
          get_idx(white, piece, square, get_lsb(pos->bitboards[K]), 0, 0);
      int16_t black_idx =
          get_idx(black, piece, square, get_lsb(pos->bitboards[k]), 0, 0);

      for (int i = 0; i < L1_SIZE; ++i)
        accumulator->psqt_accumulator[white][i] +=
            nnue->feature_weights[white_bucket][white_idx][i];

      for (int i = 0; i < L1_SIZE; ++i)
        accumulator->psqt_accumulator[black][i] +=
            nnue->feature_weights[black_bucket][black_idx][i];

      pop_bit(bitboard, square);
    }
  }

  rebuild_aux(pos, pos->mailbox, accumulator);

  int16_t *stmPsqt = accumulator->psqt_accumulator[pos->side];
  int16_t *oppPsqt = accumulator->psqt_accumulator[1 - pos->side];
  int16_t *stmAux = accumulator->aux_accumulator[pos->side];
  int16_t *oppAux = accumulator->aux_accumulator[1 - pos->side];

  uint8_t l1Neurons[L1_SIZE];
  for (int l1 = 0; l1 < L1_SIZE / 2; l1++) {
    int32_t stm_val1 = (int32_t)stmPsqt[l1] + stmAux[l1];
    int32_t stm_val2 =
        (int32_t)stmPsqt[l1 + L1_SIZE / 2] + stmAux[l1 + L1_SIZE / 2];

    const int16_t stmClipped1 = clamp(stm_val1, 0, INPUT_QUANT);
    const int16_t stmClipped2 = clamp(stm_val2, 0, INPUT_QUANT);
    l1Neurons[l1] = (stmClipped1 * stmClipped2) >> INPUT_SHIFT;

    int32_t opp_val1 = (int32_t)oppPsqt[l1] + oppAux[l1];
    int32_t opp_val2 =
        (int32_t)oppPsqt[l1 + L1_SIZE / 2] + oppAux[l1 + L1_SIZE / 2];

    const int16_t oppClipped1 = clamp(opp_val1, 0, INPUT_QUANT);
    const int16_t oppClipped2 = clamp(opp_val2, 0, INPUT_QUANT);
    l1Neurons[l1 + L1_SIZE / 2] = (oppClipped1 * oppClipped2) >> INPUT_SHIFT;
  }

  int l2Neurons[L2_SIZE] = {0};

#ifndef USE_SIMD
  for (int l1 = 0; l1 < L1_SIZE; l1++)
    for (int l2 = 0; l2 < L2_SIZE; l2++)
      l2Neurons[l2] +=
          l1Neurons[l1] * nnue->l1_weights[out_bucket][l1 * L2_SIZE + l2];
#else
  for (int l1 = 0; l1 < L1_SIZE; l1++)
    for (int l2 = 0; l2 < L2_SIZE; l2++)
      l2Neurons[l2] +=
          l1Neurons[l1] *
          nnue->l1_weights[out_bucket]
                         [(l1 / INT8_PER_INT32) * INT8_PER_INT32 * L2_SIZE +
                          l2 * INT8_PER_INT32 + (l1 % INT8_PER_INT32)];
#endif

  float l3Neurons[L3_SIZE];
  memcpy(l3Neurons, nnue->l2_bias[out_bucket], sizeof(l3Neurons));

  const float L1_NORMALISATION =
      (float)(1 << INPUT_SHIFT) / (float)(INPUT_QUANT * INPUT_QUANT * L1_QUANT);

  for (int l2 = 0; l2 < L2_SIZE; l2++) {
    const float l2Result = (float)(l2Neurons[l2]) * L1_NORMALISATION +
                           (nnue->l1_bias[out_bucket][l2]);
    const float crelu = crelu_float(l2Result);
    const float csrelu = crelu_float(l2Result * l2Result);

    for (int l3 = 0; l3 < L3_SIZE; l3++) {
      l3Neurons[l3] += crelu * nnue->l2_weights[out_bucket][l2][l3];
    }

    for (int l3 = 0; l3 < L3_SIZE; l3++) {
      l3Neurons[l3] += csrelu * nnue->l2_weights[out_bucket][l2 + L2_SIZE][l3];
    }
  }

  float result = nnue->l3_bias[out_bucket];
  for (int l3 = 0; l3 < L3_SIZE; l3++) {
    const float l3Activated = screlu_float(l3Neurons[l3]);
    result += l3Activated * nnue->l3_weights[out_bucket][l3];
  }

  return (int16_t)(result * EVAL_SCALE);
}

int nnue_evaluate(thread_t *thread, position_t *pos,
                  accumulator_t *accumulator) {
  apply_accumulator(thread, thread->ply);
  const uint8_t out_bucket = calculate_output_bucket(pos);

  const int16_t *stmPsqt = accumulator->psqt_accumulator[pos->side];
  const int16_t *oppPsqt = accumulator->psqt_accumulator[1 - pos->side];
  const int16_t *stmAux = accumulator->aux_accumulator[pos->side];
  const int16_t *oppAux = accumulator->aux_accumulator[1 - pos->side];

  simd_t *layers = &thread->neurons;

  const float L1_NORMALISATION =
      (float)(1 << INPUT_SHIFT) / (float)(INPUT_QUANT * INPUT_QUANT * L1_QUANT);

#if defined(USE_SIMD)
  const int FLOAT_VEC_SIZE = sizeof(vecf_t) / sizeof(float);
  const int I16_VEC_SIZE = sizeof(veci_t) / sizeof(int16_t);
  const int I32_STRIDE = sizeof(veci32_t) / sizeof(int32_t);

  {
    const veci_t i16_zero = zero();
    const veci_t i16_quant = set_epi16((int16_t)INPUT_QUANT);

    for (int l1 = 0; l1 < L1_SIZE / 2; l1 += 2 * I16_VEC_SIZE) {
      // STM
      veci_t psqt1 = load((const int16_t *)&stmPsqt[l1]);
      veci_t thrt1 = load((const int16_t *)&stmAux[l1]);
      veci_t c1 = clip_epi16(add_epi16(psqt1, thrt1), i16_zero, i16_quant);

      veci_t psqt2 = load((const int16_t *)&stmPsqt[l1 + L1_SIZE / 2]);
      veci_t thrt2 = load((const int16_t *)&stmAux[l1 + L1_SIZE / 2]);
      veci_t c2 = min_epi16(add_epi16(psqt2, thrt2), i16_quant);

      veci_t mul1 = mulhi_epi16(slli_epi16(c1, 16 - INPUT_SHIFT), c2);

      veci_t psqt3 = load((const int16_t *)&stmPsqt[l1 + I16_VEC_SIZE]);
      veci_t thrt3 = load((const int16_t *)&stmAux[l1 + I16_VEC_SIZE]);
      c1 = clip_epi16(add_epi16(psqt3, thrt3), i16_zero, i16_quant);

      veci_t psqt4 =
          load((const int16_t *)&stmPsqt[l1 + I16_VEC_SIZE + L1_SIZE / 2]);
      veci_t thrt4 =
          load((const int16_t *)&stmAux[l1 + I16_VEC_SIZE + L1_SIZE / 2]);
      c2 = min_epi16(add_epi16(psqt4, thrt4), i16_quant);
      veci_t mul2 = mulhi_epi16(slli_epi16(c1, 16 - INPUT_SHIFT), c2);

      vec_store_i((veci_t *)&layers->l1_neurons[l1], packus_epi16(mul1, mul2));

      // NSTM
      psqt1 = load((const int16_t *)&oppPsqt[l1]);
      thrt1 = load((const int16_t *)&oppAux[l1]);
      c1 = clip_epi16(add_epi16(psqt1, thrt1), i16_zero, i16_quant);

      psqt2 = load((const int16_t *)&oppPsqt[l1 + L1_SIZE / 2]);
      thrt2 = load((const int16_t *)&oppAux[l1 + L1_SIZE / 2]);
      c2 = min_epi16(add_epi16(psqt2, thrt2), i16_quant);
      mul1 = mulhi_epi16(slli_epi16(c1, 16 - INPUT_SHIFT), c2);

      psqt3 = load((const int16_t *)&oppPsqt[l1 + I16_VEC_SIZE]);
      thrt3 = load((const int16_t *)&oppAux[l1 + I16_VEC_SIZE]);
      c1 = clip_epi16(add_epi16(psqt3, thrt3), i16_zero, i16_quant);

      psqt4 = load((const int16_t *)&oppPsqt[l1 + I16_VEC_SIZE + L1_SIZE / 2]);
      thrt4 = load((const int16_t *)&oppAux[l1 + I16_VEC_SIZE + L1_SIZE / 2]);
      c2 = min_epi16(add_epi16(psqt4, thrt4), i16_quant);
      mul2 = mulhi_epi16(slli_epi16(c1, 16 - INPUT_SHIFT), c2);

      vec_store_i((veci_t *)&layers->l1_neurons[l1 + L1_SIZE / 2],
                  packus_epi16(mul1, mul2));
    }
  }

  const int NNZ_MAX = L1_SIZE / INT8_PER_INT32;
  const int32_t *l1Packs = (const int32_t *)layers->l1_neurons;
  uint16_t nnz_indices[NNZ_MAX + 16];
  int nnz_count = 0;

#if defined(USE_AVX512ICL)
  {
    const veci_t zero_vec = zero();
    const veci_t inc_vec = set_epi16(32);
    __m512i base_indices = _mm512_set_epi16(
        31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14,
        13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    for (int base = 0; base < NNZ_MAX; base += I32_STRIDE * 2) {
      const uint16_t lo = (uint16_t)_mm512_cmpneq_epi32_mask(
          _mm512_loadu_si512((const veci_t *)&l1Packs[base]), zero_vec);
      const uint16_t hi = (uint16_t)_mm512_cmpneq_epi32_mask(
          _mm512_loadu_si512((const veci_t *)&l1Packs[base + I32_STRIDE]),
          zero_vec);
      const uint32_t mask = _mm512_kunpackw(hi, lo);
      __m512i indices = _mm512_maskz_compress_epi16(mask, base_indices);
      _mm512_storeu_si512(&nnz_indices[nnz_count], indices);
      base_indices = _mm512_add_epi16(base_indices, inc_vec);
      nnz_count += __builtin_popcount(mask);
    }
  }
#elif defined(USE_AVX512)
  {
    const veci_t zero_vec = zero();
    const __m128i increment = _mm_set1_epi16(16);
    __m128i base_lo = _mm_setzero_si128();
    __m128i base_hi = _mm_set1_epi16(8);
    for (int i = 0; i < NNZ_MAX; i += I32_STRIDE) {
      const uint16_t mask = (uint16_t)~_mm512_cmpeq_epi32_mask(
          _mm512_loadu_si512((const veci_t *)&l1Packs[i]), zero_vec);
      const uint8_t lo = (uint8_t)(mask & 0xFF);
      _mm_storeu_si128(
          (__m128i *)&nnz_indices[nnz_count],
          _mm_add_epi16(_mm_loadu_si128((const __m128i *)NNZ_TABLE[lo]),
                        base_lo));
      nnz_count += __builtin_popcount(lo);
      const uint8_t hi = (uint8_t)(mask >> 8);
      _mm_storeu_si128(
          (__m128i *)&nnz_indices[nnz_count],
          _mm_add_epi16(_mm_loadu_si128((const __m128i *)NNZ_TABLE[hi]),
                        base_hi));
      nnz_count += __builtin_popcount(hi);
      base_lo = _mm_add_epi16(base_lo, increment);
      base_hi = _mm_add_epi16(base_hi, increment);
    }
  }
#elif defined(USE_AVX2)
  {
    const veci_t zero_vec = zero();
    const __m128i increment = _mm_set1_epi16(8);
    __m128i base_vec = _mm_setzero_si128();
    for (int i = 0; i < NNZ_MAX; i += I32_STRIDE) {
      const uint8_t byte =
          (uint8_t)(~_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(
              _mm256_loadu_si256((const veci_t *)&l1Packs[i]), zero_vec))));
      _mm_storeu_si128(
          (__m128i *)&nnz_indices[nnz_count],
          _mm_add_epi16(_mm_loadu_si128((const __m128i *)NNZ_TABLE[byte]),
                        base_vec));
      nnz_count += __builtin_popcount(byte);
      base_vec = _mm_add_epi16(base_vec, increment);
    }
  }
#elif defined(USE_NEON)
  {
    const uint32x4_t posmask = {1, 2, 4, 8};
    const uint16x8_t inc8 = vdupq_n_u16(8);
    uint16x8_t base_v = vdupq_n_u16(0);
    for (int i = 0; i < NNZ_MAX; i += 8) {
      const uint32x4_t chunk0 = vld1q_u32((const uint32_t *)&l1Packs[i]);
      const uint32_t lo =
          vaddvq_u32(vandq_u32(vtstq_u32(chunk0, chunk0), posmask));
      const uint32x4_t chunk1 = vld1q_u32((const uint32_t *)&l1Packs[i + 4]);
      const uint32_t hi =
          vaddvq_u32(vandq_u32(vtstq_u32(chunk1, chunk1), posmask));
      const uint8_t byte = (uint8_t)(lo | (hi << 4));
      vst1q_u16(&nnz_indices[nnz_count],
                vaddq_u16(vld1q_u16(NNZ_TABLE[byte]), base_v));
      nnz_count += __builtin_popcount(byte);
      base_v = vaddq_u16(base_v, inc8);
    }
  }
#endif

  const int L2_VECS = L2_SIZE / I32_STRIDE;
  veci32_t regs[L2_VECS];
  for (int r = 0; r < L2_VECS; r++)
    regs[r] = zero_i32();

  int n = 0;
  for (; n + 1 < nnz_count; n += 2) {
    const int p0 = nnz_indices[n];
    const int p1 = nnz_indices[n + 1];
    const vecs8_t u0 = broadcast_pack(l1Packs[p0]);
    const vecs8_t u1 = broadcast_pack(l1Packs[p1]);
    const int o0 = p0 * INT8_PER_INT32 * L2_SIZE;
    const int o1 = p1 * INT8_PER_INT32 * L2_SIZE;
    for (int r = 0; r < L2_VECS; r++) {
      const vecs8_t w0 =
          *((vecs8_t *)&nnue
                ->l1_weights[out_bucket][o0 + INT8_PER_INT32 * r * I32_STRIDE]);
      const vecs8_t w1 =
          *((vecs8_t *)&nnue
                ->l1_weights[out_bucket][o1 + INT8_PER_INT32 * r * I32_STRIDE]);
      regs[r] = dpbusd_epi32x2(regs[r], u0, w0, u1, w1);
    }
  }
  if (n < nnz_count) {
    const int p0 = nnz_indices[n];
    const vecs8_t u0 = broadcast_pack(l1Packs[p0]);
    const int o0 = p0 * INT8_PER_INT32 * L2_SIZE;
    for (int r = 0; r < L2_VECS; r++) {
      const vecs8_t w0 =
          *((vecs8_t *)&nnue
                ->l1_weights[out_bucket][o0 + INT8_PER_INT32 * r * I32_STRIDE]);
      regs[r] = dpbusd_epi32(regs[r], u0, w0);
    }
  }
  for (int r = 0; r < L2_VECS; r++)
    *((veci32_t *)&layers->l2_neurons[r * I32_STRIDE]) = regs[r];

  float result;
  memcpy(layers->l3_neurons, nnue->l2_bias[out_bucket],
         sizeof(layers->l3_neurons));
  {
    const vecf_t norm_ps = set_ps1(L1_NORMALISATION);
    const vecf_t one_ps = set_ps1(1.0f);
    const vecf_t zero_ps = set_ps1(0.0f);

    for (int l2 = 0; l2 < L2_SIZE / FLOAT_VEC_SIZE; l2++) {
      const vecf_t l2_result = add_ps(
          mul_ps(cvtepi32_ps(
                     *((veci32_t *)&layers->l2_neurons[l2 * FLOAT_VEC_SIZE])),
                 norm_ps),
          *((vecf_t *)&nnue->l1_bias[out_bucket][l2 * FLOAT_VEC_SIZE]));
      *((vecf_t *)&layers->l2_floats[l2 * FLOAT_VEC_SIZE]) =
          clip_ps(l2_result, one_ps, zero_ps);
      *((vecf_t *)&layers->l2_floats[l2 * FLOAT_VEC_SIZE + L2_SIZE]) =
          clip_ps(mul_ps(l2_result, l2_result), one_ps, zero_ps);
    }

    for (int l2 = 0; l2 < 2 * L2_SIZE; l2++) {
      const vecf_t act = set_ps1(layers->l2_floats[l2]);
      for (int l3 = 0; l3 < L3_SIZE / FLOAT_VEC_SIZE; l3++) {
        *((vecf_t *)&layers->l3_neurons[l3 * FLOAT_VEC_SIZE]) = fmadd_ps(
            act,
            *((vecf_t *)&nnue->l2_weights[out_bucket][l2][l3 * FLOAT_VEC_SIZE]),
            *((vecf_t *)&layers->l3_neurons[l3 * FLOAT_VEC_SIZE]));
      }
    }

    const int chunks = 64 / sizeof(vecf_t);
    vecf_t result_sums[64 / sizeof(vecf_t)];
    for (int i = 0; i < chunks; i++)
      result_sums[i] = zero_ps;

    for (int l3 = 0; l3 < L3_SIZE / FLOAT_VEC_SIZE; l3 += chunks) {
      for (int c = 0; c < chunks; c++) {
        const vecf_t clipped =
            clip_ps(*((vecf_t *)&layers->l3_neurons[(l3 + c) * FLOAT_VEC_SIZE]),
                    one_ps, zero_ps);
        result_sums[c] =
            fmadd_ps(mul_ps(clipped, clipped),
                     *((vecf_t *)&nnue
                           ->l3_weights[out_bucket][(l3 + c) * FLOAT_VEC_SIZE]),
                     result_sums[c]);
      }
    }

    result = nnue->l3_bias[out_bucket] + reduce_add_ps(result_sums);
  }

#else

  memset(layers->l2_neurons, 0, sizeof(layers->l2_neurons));

  for (int l1 = 0; l1 < L1_SIZE / 2; l1++) {
    int32_t stm_val1 = (int32_t)stmPsqt[l1] + stmAux[l1];
    int32_t stm_val2 =
        (int32_t)stmPsqt[l1 + L1_SIZE / 2] + stmAux[l1 + L1_SIZE / 2];

    const int16_t stmClipped1 = clamp(stm_val1, 0, INPUT_QUANT);
    const int16_t stmClipped2 = clamp(stm_val2, 0, INPUT_QUANT);
    layers->l1_neurons[l1] = (stmClipped1 * stmClipped2) >> INPUT_SHIFT;

    int32_t opp_val1 = (int32_t)oppPsqt[l1] + oppAux[l1];
    int32_t opp_val2 =
        (int32_t)oppPsqt[l1 + L1_SIZE / 2] + oppAux[l1 + L1_SIZE / 2];

    const int16_t oppClipped1 = clamp(opp_val1, 0, INPUT_QUANT);
    const int16_t oppClipped2 = clamp(opp_val2, 0, INPUT_QUANT);
    layers->l1_neurons[l1 + L1_SIZE / 2] =
        (oppClipped1 * oppClipped2) >> INPUT_SHIFT;
  }

  for (int l1 = 0; l1 < L1_SIZE; l1++) {
    for (int l2 = 0; l2 < L2_SIZE; l2++) {
      layers->l2_neurons[l2] += layers->l1_neurons[l1] *
                                nnue->l1_weights[out_bucket][l1 * L2_SIZE + l2];
    }
  }

  memcpy(layers->l3_neurons, nnue->l2_bias[out_bucket],
         sizeof(layers->l3_neurons));

  for (int l2 = 0; l2 < L2_SIZE; l2++) {
    const float l2Result = (float)(layers->l2_neurons[l2]) * L1_NORMALISATION +
                           (nnue->l1_bias[out_bucket][l2]);
    const float crelu = crelu_float(l2Result);
    const float csrelu = crelu_float(l2Result * l2Result);

    for (int l3 = 0; l3 < L3_SIZE; l3++) {
      layers->l3_neurons[l3] += crelu * nnue->l2_weights[out_bucket][l2][l3];
    }

    for (int l3 = 0; l3 < L3_SIZE; l3++) {
      layers->l3_neurons[l3] +=
          csrelu * nnue->l2_weights[out_bucket][l2 + L2_SIZE][l3];
    }
  }

  float result = nnue->l3_bias[out_bucket];
  for (int l3 = 0; l3 < L3_SIZE; l3++) {
    const float l3Activated = screlu_float(layers->l3_neurons[l3]);
    result += l3Activated * nnue->l3_weights[out_bucket][l3];
  }

#endif
  return (int16_t)(result * EVAL_SCALE);
}

static inline void
accumulator_addsub(accumulator_t *restrict accumulator,
                   const accumulator_t *restrict prev_accumulator,
                   uint8_t white_king_square, uint8_t black_king_square,
                   uint8_t white_bucket, uint8_t black_bucket, uint8_t piece1,
                   uint8_t piece2, uint8_t from1, uint8_t to2,
                   uint8_t color_flag) {
  const size_t white_idx_from =
      get_idx(white, piece1, from1, white_king_square, 0, 0);
  const size_t black_idx_from =
      get_idx(black, piece1, from1, black_king_square, 0, 0);
  const size_t white_idx_to =
      get_idx(white, piece2, to2, white_king_square, 0, 0);
  const size_t black_idx_to =
      get_idx(black, piece2, to2, black_king_square, 0, 0);

  __builtin_prefetch(&nnue->feature_weights[white_bucket][white_idx_from][0], 0,
                     1);
  __builtin_prefetch(&nnue->feature_weights[white_bucket][white_idx_to][0], 0,
                     1);
  __builtin_prefetch(&nnue->feature_weights[black_bucket][black_idx_from][0], 0,
                     1);
  __builtin_prefetch(&nnue->feature_weights[black_bucket][black_idx_to][0], 0,
                     1);

  if (color_flag == 0 || color_flag == 2) {
    for (int i = 0; i < L1_SIZE; ++i) {
      accumulator->psqt_accumulator[white][i] =
          prev_accumulator->psqt_accumulator[white][i] -
          nnue->feature_weights[white_bucket][white_idx_from][i] +
          nnue->feature_weights[white_bucket][white_idx_to][i];
    }
  }
  if (color_flag == 1 || color_flag == 2) {
    for (int i = 0; i < L1_SIZE; ++i) {
      accumulator->psqt_accumulator[black][i] =
          prev_accumulator->psqt_accumulator[black][i] -
          nnue->feature_weights[black_bucket][black_idx_from][i] +
          nnue->feature_weights[black_bucket][black_idx_to][i];
    }
  }
}

static inline void accumulator_addsubsub(
    accumulator_t *restrict accumulator,
    const accumulator_t *restrict prev_accumulator, uint8_t white_king_square,
    uint8_t black_king_square, uint8_t white_bucket, uint8_t black_bucket,
    uint8_t piece1, uint8_t piece2, uint8_t piece3, uint8_t from1,
    uint8_t from2, uint8_t to3, uint8_t color_flag) {
  const size_t white_idx_from1 =
      get_idx(white, piece1, from1, white_king_square, 0, 0);
  const size_t black_idx_from1 =
      get_idx(black, piece1, from1, black_king_square, 0, 0);
  const size_t white_idx_from2 =
      get_idx(white, piece2, from2, white_king_square, 0, 0);
  const size_t black_idx_from2 =
      get_idx(black, piece2, from2, black_king_square, 0, 0);
  const size_t white_idx_to =
      get_idx(white, piece3, to3, white_king_square, 0, 0);
  const size_t black_idx_to =
      get_idx(black, piece3, to3, black_king_square, 0, 0);

  if (color_flag == 0 || color_flag == 2) {
    for (int i = 0; i < L1_SIZE; ++i) {
      accumulator->psqt_accumulator[white][i] =
          prev_accumulator->psqt_accumulator[white][i] -
          nnue->feature_weights[white_bucket][white_idx_from1][i] -
          nnue->feature_weights[white_bucket][white_idx_from2][i] +
          nnue->feature_weights[white_bucket][white_idx_to][i];
    }
  }
  if (color_flag == 1 || color_flag == 2) {
    for (int i = 0; i < L1_SIZE; ++i) {
      accumulator->psqt_accumulator[black][i] =
          prev_accumulator->psqt_accumulator[black][i] -
          nnue->feature_weights[black_bucket][black_idx_from1][i] -
          nnue->feature_weights[black_bucket][black_idx_from2][i] +
          nnue->feature_weights[black_bucket][black_idx_to][i];
    }
  }
}

static inline void
accumulator_addaddsubsub(accumulator_t *restrict accumulator,
                         const accumulator_t *restrict prev_accumulator,
                         uint8_t white_king_square, uint8_t black_king_square,
                         uint8_t white_bucket, uint8_t black_bucket,
                         uint8_t piece1, uint8_t piece2, uint8_t piece3,
                         uint8_t piece4, uint8_t from1, uint8_t from2,
                         uint8_t to3, uint8_t to4, uint8_t color_flag) {
  const size_t white_idx_from1 =
      get_idx(white, piece1, from1, white_king_square, 0, 0);
  const size_t black_idx_from1 =
      get_idx(black, piece1, from1, black_king_square, 0, 0);
  const size_t white_idx_from2 =
      get_idx(white, piece2, from2, white_king_square, 0, 0);
  const size_t black_idx_from2 =
      get_idx(black, piece2, from2, black_king_square, 0, 0);
  const size_t white_idx_to1 =
      get_idx(white, piece3, to3, white_king_square, 0, 0);
  const size_t black_idx_to1 =
      get_idx(black, piece3, to3, black_king_square, 0, 0);
  const size_t white_idx_to2 =
      get_idx(white, piece4, to4, white_king_square, 0, 0);
  const size_t black_idx_to2 =
      get_idx(black, piece4, to4, black_king_square, 0, 0);

  if (color_flag == 0 || color_flag == 2) {
    for (int i = 0; i < L1_SIZE; ++i) {
      accumulator->psqt_accumulator[white][i] =
          prev_accumulator->psqt_accumulator[white][i] -
          nnue->feature_weights[white_bucket][white_idx_from1][i] -
          nnue->feature_weights[white_bucket][white_idx_from2][i] +
          nnue->feature_weights[white_bucket][white_idx_to1][i] +
          nnue->feature_weights[white_bucket][white_idx_to2][i];
    }
  }
  if (color_flag == 1 || color_flag == 2) {
    for (int i = 0; i < L1_SIZE; ++i) {
      accumulator->psqt_accumulator[black][i] =
          prev_accumulator->psqt_accumulator[black][i] -
          nnue->feature_weights[black_bucket][black_idx_from1][i] -
          nnue->feature_weights[black_bucket][black_idx_from2][i] +
          nnue->feature_weights[black_bucket][black_idx_to1][i] +
          nnue->feature_weights[black_bucket][black_idx_to2][i];
    }
  }
}

static inline void
accumulator_make_move(accumulator_t *restrict accumulator,
                      const accumulator_t *restrict prev_accumulator,
                      uint8_t white_king_square, uint8_t black_king_square,
                      uint8_t white_bucket, uint8_t black_bucket, uint8_t side,
                      int move, uint8_t moving_piece, uint8_t captured_piece,
                      uint8_t color_flag) {
  const uint8_t from = get_move_source(move);
  const uint8_t to = get_move_target(move);
  const uint8_t promoted_piece = get_move_promoted(!side, move);
  const uint8_t capture = get_move_capture(move);
  const uint8_t enpass = get_move_enpassant(move);
  const uint8_t castling = get_move_castling(move);

  if (promoted_piece) {
    const uint8_t pawn = side == 0 ? p : P;
    if (capture) {
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
    const uint8_t remove_square = to + ((side == white) ? -8 : 8);
    accumulator_addsubsub(accumulator, prev_accumulator, white_king_square,
                          black_king_square, white_bucket, black_bucket,
                          captured_piece, moving_piece, moving_piece,
                          remove_square, from, to, color_flag);
  }

  else if (capture) {
    accumulator_addsubsub(accumulator, prev_accumulator, white_king_square,
                          black_king_square, white_bucket, black_bucket,
                          captured_piece, moving_piece, moving_piece, to, from,
                          to, color_flag);
  }

  else if (castling) {
    const uint8_t mover = moving_piece >= 6;
    const uint8_t cs = castle_side(castling);
    const uint8_t rook_piece = mover == white ? R : r;
    const uint8_t ksq = from;
    const uint8_t rsq = to;
    const uint8_t kdest = castle_king_dest(mover, cs);
    const uint8_t rdest = castle_rook_dest(mover, cs);
    accumulator_addaddsubsub(accumulator, prev_accumulator, white_king_square,
                             black_king_square, white_bucket, black_bucket,
                             rook_piece, moving_piece, rook_piece, moving_piece,
                             rsq, ksq, rdest, kdest, color_flag);
  } else {
    accumulator_addsub(accumulator, prev_accumulator, white_king_square,
                       black_king_square, white_bucket, black_bucket,
                       moving_piece, moving_piece, from, to, color_flag);
  }
}

static inline void push_threat(threat_list_t *list, uint8_t w_ksq,
                               uint8_t b_ksq, int attacker_pc, int victim_pc,
                               int src, int dest) {
  int w_idx = get_threat_index(white, w_ksq, attacker_pc, victim_pc, src, dest);
  if (w_idx >= 0) {
    list->w_idx[list->w_count++] = w_idx;
  }

  int b_idx = get_threat_index(black, b_ksq, attacker_pc, victim_pc, src, dest);
  if (b_idx >= 0) {
    list->b_idx[list->b_count++] = b_idx;
  }
}

static void apply_threat_batches(accumulator_t *acc, threat_list_t *adds,
                                 threat_list_t *subs) {
  int16_t *w_acc =
      (int16_t *)__builtin_assume_aligned(acc->aux_accumulator[white], 64);
  int16_t *b_acc =
      (int16_t *)__builtin_assume_aligned(acc->aux_accumulator[black], 64);

  for (int i = 0; i < L1_SIZE; i += CHUNK_SIZE * CHUNK_ELTS) {
    vec_s16 w_vecs[CHUNK_SIZE];
    vec_s16 b_vecs[CHUNK_SIZE];

    memcpy(w_vecs, w_acc + i, sizeof(w_vecs));
    memcpy(b_vecs, b_acc + i, sizeof(b_vecs));

    for (int j = 0; j < adds->w_count; ++j) {
      for (int k = 0; k < CHUNK_SIZE; ++k) {
        vec_s8 w8 = *(vec_s8 *)&nnue
                         ->feature_aux[adds->w_idx[j]][i + (k * CHUNK_ELTS)];
        w_vecs[k] += __builtin_convertvector(w8, vec_s16);
      }
    }

    for (int j = 0; j < adds->b_count; ++j) {
      for (int k = 0; k < CHUNK_SIZE; ++k) {
        vec_s8 w8 = *(vec_s8 *)&nnue
                         ->feature_aux[adds->b_idx[j]][i + (k * CHUNK_ELTS)];
        b_vecs[k] += __builtin_convertvector(w8, vec_s16);
      }
    }

    for (int j = 0; j < subs->w_count; ++j) {
      for (int k = 0; k < CHUNK_SIZE; ++k) {
        vec_s8 w8 = *(vec_s8 *)&nnue
                         ->feature_aux[subs->w_idx[j]][i + (k * CHUNK_ELTS)];
        w_vecs[k] -= __builtin_convertvector(w8, vec_s16);
      }
    }

    for (int j = 0; j < subs->b_count; ++j) {
      for (int k = 0; k < CHUNK_SIZE; ++k) {
        vec_s8 w8 = *(vec_s8 *)&nnue
                         ->feature_aux[subs->b_idx[j]][i + (k * CHUNK_ELTS)];
        b_vecs[k] -= __builtin_convertvector(w8, vec_s16);
      }
    }

    memcpy(w_acc + i, w_vecs, sizeof(w_vecs));
    memcpy(b_acc + i, b_vecs, sizeof(b_vecs));
  }
}

static inline uint64_t get_piece_attacks_fast(int pc, int sq, uint64_t occ) {
  switch (pc) {
  case P: return (sq >= 8 && sq <= 55) ? get_pawn_attacks(white, sq) : 0;
  case p: return (sq >= 8 && sq <= 55) ? get_pawn_attacks(black, sq) : 0;
  case N: case n: return get_knight_attacks(sq);
  case B: case b: return get_bishop_attacks(sq, occ);
  case R: case r: return get_rook_attacks(sq, occ);
  case Q: case q: return get_queen_attacks(sq, occ);
  default: return 0;
  }
}

static void process_changed_squares(position_t *pos, uint64_t changed_sqs, threat_list_t *list) {
  uint64_t occ = pos->occupancies[both];
  uint8_t w_ksq = get_lsb(pos->bitboards[K]);
  uint8_t b_ksq = get_lsb(pos->bitboards[k]);
  uint64_t non_kings = occ & ~(pos->bitboards[K] | pos->bitboards[k]);

  uint64_t sqs = changed_sqs & non_kings;
  while (sqs) {
    int src = poplsb(&sqs);
    int pc = pos->mailbox[src];

    uint64_t attacks = get_piece_attacks_fast(pc, src, occ) & non_kings;
    while (attacks) {
      int dest = poplsb(&attacks);
      int victim = pos->mailbox[dest];
      push_threat(list, w_ksq, b_ksq, pc, victim, src, dest);
    }
  }

  sqs = changed_sqs & non_kings;
  while (sqs) {
    int dest = poplsb(&sqs);
    int victim = pos->mailbox[dest];

    uint64_t attackers = attackers_to(pos, dest, occ) & non_kings & ~changed_sqs;
    while (attackers) {
      int src = poplsb(&attackers);
      int pc = pos->mailbox[src];
      push_threat(list, w_ksq, b_ksq, pc, victim, src, dest);
    }
  }
}

static void process_slider_deltas(position_t *pos_before, position_t *pos_after,
                                  uint64_t affected_sliders, uint64_t changed_sqs,
                                  threat_list_t *adds, threat_list_t *subs) {
  uint64_t occ_b = pos_before->occupancies[both];
  uint64_t occ_a = pos_after->occupancies[both];
  uint8_t w_ksq = get_lsb(pos_after->bitboards[K]);
  uint8_t b_ksq = get_lsb(pos_after->bitboards[k]);

  uint64_t non_kings_b = occ_b & ~(pos_before->bitboards[K] | pos_before->bitboards[k]);
  uint64_t non_kings_a = occ_a & ~(pos_after->bitboards[K] | pos_after->bitboards[k]);

  uint64_t sqs = affected_sliders & non_kings_a;
  while (sqs) {
    int src = poplsb(&sqs);
    int pc = pos_after->mailbox[src];

    uint64_t old_attacks = get_piece_attacks_fast(pc, src, occ_b) & non_kings_b & ~changed_sqs;
    uint64_t new_attacks = get_piece_attacks_fast(pc, src, occ_a) & non_kings_a & ~changed_sqs;

    uint64_t dropped = old_attacks & ~new_attacks;
    uint64_t gained = new_attacks & ~old_attacks;

    while (dropped) {
      int dest = poplsb(&dropped);
      int victim = pos_before->mailbox[dest];
      push_threat(subs, w_ksq, b_ksq, pc, victim, src, dest);
    }

    while (gained) {
      int dest = poplsb(&gained);
      int victim = pos_after->mailbox[dest];
      push_threat(adds, w_ksq, b_ksq, pc, victim, src, dest);
    }
  }
}

static void update_aux_incremental(accumulator_t *acc,
                                       position_t *pos_before,
                                       position_t *pos_after) {
  uint64_t real_changed_sqs = 0;
  for (int i = 0; i < 12; i++) {
    real_changed_sqs |= (pos_before->bitboards[i] ^ pos_after->bitboards[i]);
  }

  uint64_t occ_b = pos_before->occupancies[both];
  uint64_t occ_a = pos_after->occupancies[both];
  uint64_t b_sliders_b = pos_before->bitboards[B] | pos_before->bitboards[b] | pos_before->bitboards[Q] | pos_before->bitboards[q];
  uint64_t r_sliders_b = pos_before->bitboards[R] | pos_before->bitboards[r] | pos_before->bitboards[Q] | pos_before->bitboards[q];
  uint64_t b_sliders_a = pos_after->bitboards[B] | pos_after->bitboards[b] | pos_after->bitboards[Q] | pos_after->bitboards[q];
  uint64_t r_sliders_a = pos_after->bitboards[R] | pos_after->bitboards[r] | pos_after->bitboards[Q] | pos_after->bitboards[q];

  uint64_t sliders_before = 0;
  uint64_t sliders_after = 0;
  uint64_t changed_copy = real_changed_sqs;

  while (changed_copy) {
    int sq = poplsb(&changed_copy);
    sliders_before |= get_bishop_attacks(sq, occ_b) & b_sliders_b;
    sliders_before |= get_rook_attacks(sq, occ_b) & r_sliders_b;
    
    sliders_after |= get_bishop_attacks(sq, occ_a) & b_sliders_a;
    sliders_after |= get_rook_attacks(sq, occ_a) & r_sliders_a;
  }

  uint64_t affected_sliders = (sliders_before | sliders_after) & ~real_changed_sqs;

  threat_list_t adds = {.w_count = 0, .b_count = 0};
  threat_list_t subs = {.w_count = 0, .b_count = 0};

  process_changed_squares(pos_before, real_changed_sqs, &subs);
  process_changed_squares(pos_after, real_changed_sqs, &adds);
  process_slider_deltas(pos_before, pos_after, affected_sliders, real_changed_sqs, &adds, &subs);

  apply_threat_batches(acc, &adds, &subs);
}

void apply_accumulator(thread_t *thread, int ply) {
  if (ply == 0 || !thread->lazy[ply].dirty)
    return;

  apply_accumulator(thread, ply - 1);

  lazy_acc_state_t *s = &thread->lazy[ply];

  if (s->psqt_needs_refresh) {
    position_t tmp;
    tmp.side = s->side;
    memcpy(tmp.bitboards, s->bitboards, 12 * sizeof(uint64_t));
    tmp.occupancies[white] = tmp.bitboards[P] | tmp.bitboards[N] |
                             tmp.bitboards[B] | tmp.bitboards[R] |
                             tmp.bitboards[Q] | tmp.bitboards[K];
    tmp.occupancies[black] = tmp.bitboards[p] | tmp.bitboards[n] |
                             tmp.bitboards[b] | tmp.bitboards[r] |
                             tmp.bitboards[q] | tmp.bitboards[k];
    tmp.occupancies[both] = tmp.occupancies[white] | tmp.occupancies[black];

    memset(tmp.mailbox, 12, 64);
    for (int i = 0; i < 12; i++) {
      uint64_t bb = s->bitboards[i];
      while (bb)
        tmp.mailbox[poplsb(&bb)] = i;
    }

    refresh_accumulator(thread, &tmp, &thread->accumulator[ply]);

    uint8_t opp = s->color_flag;
    memcpy(thread->accumulator[ply].psqt_accumulator[opp],
           thread->accumulator[ply - 1].psqt_accumulator[opp],
           L1_SIZE * sizeof(int16_t));

    accumulator_make_move(
        &thread->accumulator[ply], &thread->accumulator[ply - 1],
        s->white_king_sq, s->black_king_sq, s->white_bucket, s->black_bucket,
        s->side, s->move, s->moving_piece, s->captured_piece, s->color_flag);
  } else {
    accumulator_make_move(
        &thread->accumulator[ply], &thread->accumulator[ply - 1],
        s->white_king_sq, s->black_king_sq, s->white_bucket, s->black_bucket,
        s->side, s->move, s->moving_piece, s->captured_piece, both);
  }

  if (s->threat_needs_refresh) {
    for (int i = 0; i < L1_SIZE; ++i) {
      thread->accumulator[ply].aux_accumulator[white][i] = 0;
      thread->accumulator[ply].aux_accumulator[black][i] = 0;
    }
    rebuild_aux(&thread->positions[ply], thread->positions[ply].mailbox,
                    &thread->accumulator[ply]);
  } else {

    memcpy(thread->accumulator[ply].aux_accumulator[white],
           thread->accumulator[ply - 1].aux_accumulator[white],
           L1_SIZE * sizeof(int16_t));
    memcpy(thread->accumulator[ply].aux_accumulator[black],
           thread->accumulator[ply - 1].aux_accumulator[black],
           L1_SIZE * sizeof(int16_t));

    update_aux_incremental(&thread->accumulator[ply],
                               &thread->positions[ply - 1],
                               &thread->positions[ply]);
  }

  s->dirty = 0;
}

void null_move_copy_accumulator(thread_t *thread, int src_ply, int dst_ply) {
  apply_accumulator(thread, src_ply);
  thread->accumulator[dst_ply] = thread->accumulator[src_ply];
  thread->lazy[dst_ply].dirty = 0;
}

void update_nnue(position_t *pos, thread_t *thread, uint8_t mailbox_copy[64],
                 uint16_t move) {
  lazy_acc_state_t *state = &thread->lazy[thread->ply];
  const uint8_t from = get_move_source(move);
  const uint8_t to = get_move_target(move);

  state->dirty = 1;
  state->move = move;
  state->side = pos->side;
  state->white_king_sq = get_lsb(pos->bitboards[K]);
  state->black_king_sq = get_lsb(pos->bitboards[k]);
  state->white_bucket = get_king_bucket(white, state->white_king_sq);
  state->black_bucket = get_king_bucket(black, state->black_king_sq);
  state->moving_piece = mailbox_copy[from];

  if (get_move_enpassant(move)) {
    const uint8_t ep_sq = to + ((pos->side == white) ? -8 : 8);
    state->captured_piece = mailbox_copy[ep_sq];
  } else {
    state->captured_piece = mailbox_copy[to];
  }

  state->psqt_needs_refresh = 0;
  state->threat_needs_refresh = 0;

  if (state->moving_piece == K || state->moving_piece == k) {
    uint8_t side = state->moving_piece >= 6;
    uint8_t kdest =
        get_move_castling(move)
            ? castle_king_dest(side, castle_side(get_move_castling(move)))
            : to;

    uint8_t source_flip = (from & 7) >= 4;
    uint8_t target_flip = (kdest & 7) >= 4;
    uint8_t bucket_changed =
        get_king_bucket(side, from) != get_king_bucket(side, kdest);
    uint8_t mirror_flipped = source_flip != target_flip;

    state->psqt_needs_refresh = bucket_changed || mirror_flipped;
    state->threat_needs_refresh = mirror_flipped;
  }

  state->color_flag =
      state->psqt_needs_refresh ? (pos->side == black ? black : white) : both;

  if (state->psqt_needs_refresh) {
    memcpy(state->bitboards, pos->bitboards, 12 * sizeof(uint64_t));
  }
}
