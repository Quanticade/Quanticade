#ifndef NNUE_H
#define NNUE_H

#include "structs.h"

extern nnue_settings_t nnue_settings;

#define INPUT_WEIGHTS 768
#define HIDDEN_SIZE 1536
#define SCALE 400
#define L1Q 255
#define OutputQ 64

typedef struct nnue {
  _Alignas(32) int16_t feature_weights[INPUT_WEIGHTS][HIDDEN_SIZE];
  _Alignas(32) int16_t feature_bias[HIDDEN_SIZE];
  _Alignas(32) int16_t output_weights[2][HIDDEN_SIZE];
  _Alignas(32) int16_t output_bias;
} nnue_t;

extern nnue_t nnue;

void nnue_init(const char *nnue_file_name);
void init_accumulator(position_t *pos);
int nnue_evaluate(position_t *pos);
int nnue_eval_pos(position_t *pos);
void accumulator_make_move(position_t *pos, int move, uint8_t *mailbox);

#endif
