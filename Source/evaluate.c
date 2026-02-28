#include "evaluate.h"
#include "bitboards.h"
#include "enums.h"
#include "nnue.h"
#include "structs.h"
#include "utils.h"
#include <stdio.h>

nnue_t nnue_data;
extern nnue_settings_t nnue_settings;

int EVAL_KNIGHT = 384;
int EVAL_BISHOP = 384;
int EVAL_ROOK = 640;
int EVAL_QUEEN = 1280;
int EVAL_SCALE_BASE = 25600;

extern uint8_t disable_norm;

int16_t evaluate(thread_t *thread, position_t *pos,
                 accumulator_t *accumulator) {
  if (accumulator->dirty) {
    uint8_t ply = pos->ply;
    while (thread->accumulator[ply].dirty) {
      ply--;
    }
    for (int i = ply+1; i <= pos->ply; i++) {
      uint8_t white_bucket = get_king_bucket(white, thread->accumulator[i].white_king);
      uint8_t black_bucket = get_king_bucket(black, thread->accumulator[i].black_king);
      accumulator_make_move(&thread->accumulator[i],
                          &thread->accumulator[i - 1], thread->accumulator[i].white_king,
                          thread->accumulator[i].black_king, white_bucket, black_bucket,
                          thread->accumulator[i].side, thread->accumulator[i].move, thread->accumulator[i].mailbox, both);
      thread->accumulator[i].dirty = 0;
    }
  }
  int eval = nnue_evaluate(thread, pos, &thread->accumulator[pos->ply]);

  if (!disable_norm) {
    int phase = EVAL_KNIGHT * popcount(pos->bitboards[n] | pos->bitboards[N]) +
                EVAL_BISHOP * popcount(pos->bitboards[b] | pos->bitboards[B]) +
                EVAL_ROOK * popcount(pos->bitboards[r] | pos->bitboards[R]) +
                EVAL_QUEEN * popcount(pos->bitboards[q] | pos->bitboards[Q]);

    eval = eval * (EVAL_SCALE_BASE + phase) / 32768;
  }

  int16_t final_eval = clamp(eval, -MATE_SCORE + 1, MATE_SCORE - 1);
  return final_eval;
}
