#ifndef WDL_H
#define WDL_H

#include <stdint.h>

typedef struct {
    double a;
    double b;
} wdl_params_t;

typedef struct {
    double win;
    double loss;
} wdl_model_result_t;

wdl_model_result_t wdl_model(int16_t pov_score, uint16_t material);

int16_t wdl_normalize_score(int16_t score, uint16_t material);
int16_t wdl_unnormalize_score(int16_t score, uint16_t material);

#endif
