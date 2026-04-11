#ifndef NNUE_H
#define NNUE_H

#include "structs.h"
#include "arch.h"
#include <stdint.h>

extern nnue_settings_t nnue_settings;

typedef struct nnue {
  _Alignas(64) int16_t
      feature_weights[KING_BUCKETS][INPUT_WEIGHTS][L1_SIZE];
  _Alignas(64) int16_t feature_bias[L1_SIZE];
  _Alignas(64) int8_t l1_weights[OUTPUT_BUCKETS][L1_SIZE * L2_SIZE];
  _Alignas(64) float l1_bias[OUTPUT_BUCKETS][L2_SIZE];
  _Alignas(64) float l2_weights[OUTPUT_BUCKETS][2*L2_SIZE][L3_SIZE];
  _Alignas(64) float l2_bias[OUTPUT_BUCKETS][L3_SIZE];
  _Alignas(64) float l3_weights[OUTPUT_BUCKETS][L3_SIZE];
  _Alignas(64) float l3_bias[OUTPUT_BUCKETS];
} nnue_t;

extern nnue_t nnue;

void nnue_init(const char *nnue_file_name);
void init_accumulator(position_t *pos, accumulator_t *accumulator);
void init_finny_tables(thread_t *thread, position_t *pos);
int nnue_evaluate(thread_t *thread, position_t *pos, accumulator_t *accumulator);
int nnue_eval_pos(position_t *pos, accumulator_t *accumulator);
void update_nnue(position_t *pos, thread_t *thread, uint8_t mailbox_copy[64],
                 uint16_t move);
void apply_accumulator(thread_t *thread, int ply);
void null_move_copy_accumulator(thread_t *thread, int src_ply, int dst_ply);

#endif