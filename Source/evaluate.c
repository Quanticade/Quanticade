#include "evaluate.h"
#include "bitboards.h"
#include "enums.h"
#include "nnue.h"
#include "structs.h"
#include "uci.h"

nnue_t nnue_data;
extern nnue_settings_t nnue_settings;
extern int SEEPieceValues[];

int evaluate(position_t *pos, accumulator_t *accumulator) {
  int eval = nnue_evaluate(pos, accumulator);

  int phase = SEEPieceValues[KNIGHT] * popcount(pos->bitboards[n] | pos->bitboards[N]) +
              SEEPieceValues[BISHOP] * popcount(pos->bitboards[b] | pos->bitboards[B]) +
              SEEPieceValues[ROOK] * popcount(pos->bitboards[r] | pos->bitboards[R]) +
              SEEPieceValues[QUEEN] * popcount(pos->bitboards[q] | pos->bitboards[Q]);

  eval = eval * (26500 + phase) / 32768;
  float fifty_move_scaler = (float)((100 - (float)pos->fifty) / 100);
  fifty_move_scaler = MAX(fifty_move_scaler, 0.5f);
  return (int)(eval * fifty_move_scaler);
}
