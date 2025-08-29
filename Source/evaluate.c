#include "evaluate.h"
#include "bitboards.h"
#include "enums.h"
#include "nnue.h"
#include "structs.h"
#include "uci.h"
#include "utils.h"

nnue_t nnue_data;
extern nnue_settings_t nnue_settings;

int16_t evaluate(thread_t *thread, position_t *pos,
                 accumulator_t *accumulator) {
  int eval = nnue_evaluate(thread, pos, accumulator);

  const int material = 3 * popcount(pos->bitboards[n] | pos->bitboards[N]) +
                       3 * popcount(pos->bitboards[b] | pos->bitboards[B]) +
                       5 * popcount(pos->bitboards[r] | pos->bitboards[R]) +
                       10 * popcount(pos->bitboards[q] | pos->bitboards[Q]);

  eval = (eval * (20000 + material) +
          thread->optimism[pos->side] * (2000 - material)) /
         32768;
  eval = clamp(eval, -MATE_SCORE + 1, MATE_SCORE - 1);
  return (int16_t)eval;
}
