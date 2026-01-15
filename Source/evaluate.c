#include "evaluate.h"
#include "bitboards.h"
#include "enums.h"
#include "nnue.h"
#include "structs.h"
#include "utils.h"

nnue_t nnue_data;
extern nnue_settings_t nnue_settings;

int16_t evaluate(thread_t *thread, position_t *pos, accumulator_t *accumulator) {
    (void)thread;
    (void)accumulator;
    int eval = 0;

    // White pieces
    eval += popcount(pos->bitboards[P]) * 100;
    eval += popcount(pos->bitboards[N]) * 300;
    eval += popcount(pos->bitboards[B]) * 330;
    eval += popcount(pos->bitboards[R]) * 500;
    eval += popcount(pos->bitboards[Q]) * 900;

    // Black pieces
    eval -= popcount(pos->bitboards[p]) * 100;
    eval -= popcount(pos->bitboards[n]) * 300;
    eval -= popcount(pos->bitboards[b]) * 330;
    eval -= popcount(pos->bitboards[r]) * 500;
    eval -= popcount(pos->bitboards[q]) * 900;

  int16_t final_eval = clamp(eval, -MATE_SCORE + 1, MATE_SCORE - 1);
  return pos->side == white ? final_eval : -final_eval;
}
