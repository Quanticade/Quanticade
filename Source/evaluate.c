#include "evaluate.h"
#include "bitboards.h"
#include "enums.h"
#include "nnue.h"
#include "structs.h"
#include "utils.h"

nnue_t nnue_data;
extern nnue_settings_t nnue_settings;

int16_t evaluate(thread_t *thread, position_t *pos, accumulator_t *accumulator) {
    int eval = nnue_evaluate(thread, pos, accumulator);

  int16_t final_eval = clamp(eval, -MATE_SCORE + 1, MATE_SCORE - 1);
  return final_eval;
}
