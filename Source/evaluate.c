#include "evaluate.h"
#include "bitboards.h"
#include "enums.h"
#include "nnue.h"
#include "structs.h"
#include "uci.h"

nnue_t nnue_data;
extern nnue_settings_t nnue_settings;

int evaluate(position_t *pos, accumulator_t *accumulator) {
  int eval = nnue_evaluate(pos, accumulator);

  int phase = 3 * popcount(pos->bitboards[n] | pos->bitboards[N]) +
              3 * popcount(pos->bitboards[b] | pos->bitboards[B]) +
              5 * popcount(pos->bitboards[r] | pos->bitboards[R]) +
              10 * popcount(pos->bitboards[q] | pos->bitboards[Q]);

  eval = eval * (200 + phase) / 256;
  float fifty_move_scaler = (float)((100 - (float)pos->fifty) / 100);
  fifty_move_scaler = MAX(fifty_move_scaler, 0.5f);
  return (int)(eval * fifty_move_scaler);
}
