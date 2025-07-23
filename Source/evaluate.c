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
  for (uint8_t sq = 0; sq < 64; sq++) {
    if (pos->side == 0) {
      switch (pos->mailbox[sq]) {
        case P: eval += 100; break;
        case p: eval -= 100; break;
        case N: eval += 300; break;
        case n: eval -= 300; break;
        case B: eval += 330; break;
        case b: eval -= 330; break;
        case R: eval += 500; break;
        case r: eval -= 500; break;
        case Q: eval += 900; break;
        case q: eval -= 900; break;
        default: break;
      }
    }
    else {
      switch (pos->mailbox[sq]) {
        case P: eval -= 100; break;
        case p: eval += 100; break;
        case N: eval -= 300; break;
        case n: eval += 300; break;
        case B: eval -= 330; break;
        case b: eval += 330; break;
        case R: eval -= 500; break;
        case r: eval += 500; break;
        case Q: eval -= 900; break;
        case q: eval += 900; break;
        default: break;
      }
    }
  }


  int16_t final_eval = clamp(eval, -MATE_SCORE + 1, MATE_SCORE - 1);
  return final_eval;
}
