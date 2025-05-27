#ifndef NNUE_H
#define NNUE_H

#include "structs.h"
#include <stdint.h>

extern nnue_settings_t nnue_settings;

#define INPUT_WEIGHTS 768
#define L1_SIZE 1536
#define L2_SIZE 16
#define L3_SIZE 32
#define OUTPUT_BUCKETS 8
#define KING_BUCKETS 13
#define SCALE 400
#define INPUT_QUANT 255
#define L1_QUANT 64
#define INPUT_SHIFT 10

typedef struct nnue {
  _Alignas(64) int16_t
      feature_weights[KING_BUCKETS][INPUT_WEIGHTS][L1_SIZE];
  _Alignas(64) int16_t feature_bias[L1_SIZE];
  _Alignas(64) int16_t l1_weights[OUTPUT_BUCKETS][L1_SIZE][L2_SIZE];
  _Alignas(64) int16_t l1_bias[OUTPUT_BUCKETS][L2_SIZE];
  _Alignas(64) float l2_weights[OUTPUT_BUCKETS][L2_SIZE][L3_SIZE];
  _Alignas(64) float l2_bias[OUTPUT_BUCKETS][L3_SIZE];
  _Alignas(64) float l3_weights[OUTPUT_BUCKETS][L3_SIZE];
  _Alignas(64) float l3_bias[OUTPUT_BUCKETS];
} nnue_t;

extern nnue_t nnue;

void nnue_init(const char *nnue_file_name);
void init_accumulator(position_t *pos, accumulator_t *accumulator);
void init_finny_tables(thread_t *thread, position_t *pos);
int nnue_evaluate(position_t *pos, accumulator_t *accumulator);
int nnue_eval_pos(position_t *pos, accumulator_t *accumulator);
void update_nnue(position_t *pos, thread_t *thread, uint8_t mailbox_copy[64],
                 uint16_t move);

#endif
