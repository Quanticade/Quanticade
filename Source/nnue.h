#ifndef NNUE_H
#define NNUE_H

#include "structs.h"

extern nnue_settings_t nnue_settings;

#define INPUT_WEIGHTS 768
#define HIDDEN_SIZE 2048
#define OUTPUT_BUCKETS 8
#define KING_BUCKETS 13
#define SCALE 400
#define L1Q 255
#define OutputQ 64

typedef struct nnue {
  _Alignas(64) int16_t
      feature_weights[KING_BUCKETS][INPUT_WEIGHTS][HIDDEN_SIZE];
  _Alignas(64) int16_t feature_bias[HIDDEN_SIZE];
  _Alignas(64) int16_t output_weights[OUTPUT_BUCKETS][2][HIDDEN_SIZE];
  _Alignas(64) int16_t output_bias[OUTPUT_BUCKETS];
} nnue_t;

extern nnue_t nnue;

void nnue_init(const char *nnue_file_name);
void init_accumulator(position_t *pos, accumulator_t *accumulator);
void init_finny_tables(position_t *pos);
int nnue_evaluate(position_t *pos, accumulator_t *accumulator);
int nnue_eval_pos(position_t *pos, accumulator_t *accumulator);
void update_nnue(position_t *pos, thread_t *thread, uint8_t mailbox_copy[64],
                 uint16_t move);

#endif
