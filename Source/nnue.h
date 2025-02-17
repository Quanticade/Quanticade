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
  _Alignas(64) int16_t feature_weights[KING_BUCKETS][INPUT_WEIGHTS][HIDDEN_SIZE];
  _Alignas(64) int16_t feature_bias[HIDDEN_SIZE];
  _Alignas(64) int16_t output_weights[OUTPUT_BUCKETS][2][HIDDEN_SIZE];
  _Alignas(64) int16_t output_bias[OUTPUT_BUCKETS];
} nnue_t;

extern nnue_t nnue;

uint8_t get_king_bucket(uint8_t square);
uint8_t need_refresh(uint8_t from, uint8_t to);
void nnue_init(const char *nnue_file_name);
void init_accumulator(position_t *pos, accumulator_t *accumulator);
int nnue_evaluate(position_t *pos, accumulator_t *accumulator);
int nnue_eval_pos(position_t *pos, accumulator_t *accumulator);
void accumulator_make_move(accumulator_t *accumulator,
                           accumulator_t *prev_accumulator, uint8_t bucket, uint8_t side,
                           int move, uint8_t *mailbox);

#endif
