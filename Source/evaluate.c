#include "evaluate.h"
#include "bitboards.h"
#include "enums.h"
#include "nnue.h"
#include "structs.h"

nnue_t nnue_data;
extern nnue_settings_t nnue_settings;

int evaluate(position_t *pos, accumulator_t *accumulator) {
  int eval = nnue_evaluate(accumulator, pos->side);

  int phase = 3 * popcount(pos->bitboards[n] | pos->bitboards[N]) +
              3 * popcount(pos->bitboards[b] | pos->bitboards[B]) +
              5 * popcount(pos->bitboards[r] | pos->bitboards[R]) +
              10 * popcount(pos->bitboards[q] | pos->bitboards[Q]);

  eval = eval * (206 + phase) / 256;
  return (int)(eval * (float)((100 - (float)pos->fifty) / 100));
}
