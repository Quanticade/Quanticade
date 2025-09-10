#include "evaluate.h"
#include "bitboards.h"
#include "enums.h"
#include "nnue.h"
#include "structs.h"
#include "utils.h"

nnue_t nnue_data;
extern nnue_settings_t nnue_settings;

int EVAL_KNIGHT = 389;
int EVAL_BISHOP = 374;
int EVAL_ROOK = 616;
int EVAL_QUEEN = 1210;
int EVAL_SCALE_BASE = 25255;

int16_t evaluate(thread_t *thread, position_t *pos,
                 accumulator_t *accumulator) {
  int eval = nnue_evaluate(thread, pos, accumulator);

  int phase = EVAL_KNIGHT * popcount(pos->bitboards[n] | pos->bitboards[N]) +
              EVAL_BISHOP * popcount(pos->bitboards[b] | pos->bitboards[B]) +
              EVAL_ROOK * popcount(pos->bitboards[r] | pos->bitboards[R]) +
              EVAL_QUEEN * popcount(pos->bitboards[q] | pos->bitboards[Q]);

  eval = eval * (EVAL_SCALE_BASE + phase) / 32768;
  eval = clamp(eval, -MATE_SCORE + 1, MATE_SCORE - 1);
  return (int16_t)eval;
}
