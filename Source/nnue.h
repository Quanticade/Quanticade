#ifndef NNUE_H
#define NNUE_H

#include "structs.h"

extern nnue_settings_t nnue_settings;

#define INPUT_WEIGHTS 768
#define HIDDEN_SIZE   1024
#define SCALE         400
#define L1Q           255
#define OutputQ       64

typedef struct nnue {
    int16_t feature_weights[INPUT_WEIGHTS][HIDDEN_SIZE];
    int16_t feature_bias[HIDDEN_SIZE];
    int16_t output_weights[2][HIDDEN_SIZE];
    int16_t output_bias;
} nnue_t;

typedef struct accumulator {
    int16_t accumulator[2][HIDDEN_SIZE];
} accumulator_t;

extern nnue_t nnue;
extern accumulator_t accumulator;

void nnue_init(const char *nnue_file_name);
int nnue_eval_pos(position_t *pos);

#endif
