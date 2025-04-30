#include "spsa.h"
#include "enums.h"
#include "search.h"
#include "structs.h"
#include "uci.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

spsa_t spsa[100];

uint8_t spsa_index = 0;

// search.c
extern int LMP_BASE;
extern int RAZOR_DEPTH;
extern int RAZOR_MARGIN;
extern int RFP_DEPTH;
extern int RFP_MARGIN;
extern int FP_DEPTH;
extern int FP_MULTIPLIER;
extern int FP_ADDITION;
extern int NMP_BASE_REDUCTION;
extern int NMP_DIVISER;
extern int NMP_RED_DIVISER;
extern int NMP_RED_MIN;
extern int IIR_DEPTH;
extern int SEE_QUIET;
extern int SEE_CAPTURE;
extern int SEE_DEPTH;
extern int SE_DEPTH;
extern int SE_DEPTH_REDUCTION;
extern int SE_PV_DOUBLE_MARGIN;
extern int SE_TRIPLE_MARGIN;
extern int LMR_PV_NODE;
extern int LMR_HISTORY_QUIET;
extern int LMR_HISTORY_NOISY;
extern int LMR_IN_CHECK;
extern int LMR_CUTNODE;
extern int LMR_TT_DEPTH;
extern int LMR_TT_PV;
extern int ASP_WINDOW;
extern int ASP_DEPTH;
extern int QS_SEE_THRESHOLD;
extern int MO_SEE_THRESHOLD;
extern double ASP_MULTIPLIER;
extern int LMR_QUIET_HIST_DIV;
extern int LMR_CAPT_HIST_DIV;
extern double LMR_OFFSET_QUIET;
extern double LMR_DIVISOR_QUIET;
extern double LMR_OFFSET_NOISY;
extern double LMR_DIVISOR_NOISY;

// history.c
extern int CAPTURE_HISTORY_BONUS_MAX;
extern int QUIET_HISTORY_BONUS_MAX;
extern int CONT_HISTORY_BONUS_MAX;
extern int CAPTURE_HISTORY_MALUS_MAX;
extern int QUIET_HISTORY_MALUS_MAX;
extern int CONT_HISTORY_MALUS_MAX;
extern int CAPTURE_HISTORY_BONUS_MIN;
extern int QUIET_HISTORY_BONUS_MIN;
extern int CONT_HISTORY_BONUS_MIN;
extern int CAPTURE_HISTORY_MALUS_MIN;
extern int QUIET_HISTORY_MALUS_MIN;
extern int CONT_HISTORY_MALUS_MIN;
extern int CORR_HISTORY_MINMAX;
extern int PAWN_CORR_HISTORY_MULTIPLIER;
extern int HISTORY_MAX;

// TM
extern double DEF_TIME_MULTIPLIER;
extern double DEF_INC_MULTIPLIER;
extern double MAX_TIME_MULTIPLIER;
extern double HARD_LIMIT_MULTIPLIER;
extern double SOFT_LIMIT_MULTIPLIER;
extern double NODE_TIME_MULTIPLIER;
extern double NODE_TIME_ADDITION;
extern double NODE_TIME_MIN;

extern int mvv[];
extern int SEEPieceValues[];

extern double bestmove_scale[5];
extern double eval_scale[5];

#define RATE(VALUE) MAX(0.5, MAX(1, (((float)VALUE * 2) - 1)) / 20)
#define RATE_POISON(VALUE) MAX(0.5, MAX(1, (((float)VALUE * 2) - 1)) / 20 / 5)
#define RATE_DOUBLE(VALUE) MAX(0.05, MAX(1, ((VALUE * 2) - 1)) / 20)
#define RATE_DOUBLE_TIME(VALUE) MAX(0.001, (MAX(1, ((VALUE * 2) - 1)) / 20) / 5)
#define SPSA_MAX(VALUE) VALUE * 2
#define STRINGIFY(VARIABLE) (#VARIABLE)

void add_double_spsa(char name[], double *value, double min, double max,
                     double rate, void (*func)(void), uint8_t tunable) {
  strcpy(spsa[spsa_index].name, name);
  spsa[spsa_index].value = value;
  spsa[spsa_index].min.min_float = min;
  spsa[spsa_index].max.max_float = max;
  spsa[spsa_index].rate = rate;
  spsa[spsa_index].is_float = 1;
  spsa[spsa_index].func = func;
  spsa[spsa_index].tunable = tunable;
  spsa_index++;
}

void add_int_spsa(char name[], int *value, int min, int max, double rate,
                  void (*func)(void), uint8_t tunable) {
  strcpy(spsa[spsa_index].name, name);
  spsa[spsa_index].value = value;
  spsa[spsa_index].min.min_int = min;
  spsa[spsa_index].max.max_int = max;
  spsa[spsa_index].rate = rate;
  spsa[spsa_index].is_float = 0;
  spsa[spsa_index].func = func;
  spsa[spsa_index].tunable = tunable;
  spsa_index++;
}

#define SPSA_INT(VARIABLE, TUNABLE)                                            \
  add_int_spsa(STRINGIFY(VARIABLE), &VARIABLE, 1, SPSA_MAX(VARIABLE),          \
               RATE(VARIABLE), NULL, TUNABLE)
#define SPSA_INT_POISON(VARIABLE, TUNABLE)                                     \
  add_int_spsa(STRINGIFY(VARIABLE), &VARIABLE, 1, SPSA_MAX(VARIABLE),          \
              RATE_POISON(VARIABLE), NULL, TUNABLE)
#define SPSA_INT_FUNC(VARIABLE, FUNC, TUNABLE)                                 \
  add_int_spsa(STRINGIFY(VARIABLE), &VARIABLE, 1, SPSA_MAX(VARIABLE),          \
               RATE(VARIABLE), FUNC, TUNABLE)
#define SPSA_INT_NAME(NAME, VARIABLE, TUNABLE)                                 \
  add_int_spsa(NAME, &VARIABLE, 1, SPSA_MAX(VARIABLE), RATE(VARIABLE), NULL,   \
               TUNABLE)

void init_spsa_table(void) {
  SPSA_INT(LMP_BASE, 1);
  SPSA_INT_POISON(RAZOR_DEPTH, 1);
  SPSA_INT(RAZOR_MARGIN, 1);
  SPSA_INT_POISON(RFP_DEPTH, 1);
  SPSA_INT(RFP_MARGIN, 1);
  SPSA_INT_POISON(FP_DEPTH, 1);
  SPSA_INT(FP_MULTIPLIER, 1);
  SPSA_INT(FP_ADDITION, 1);
  SPSA_INT_POISON(NMP_BASE_REDUCTION, 1);
  SPSA_INT(NMP_DIVISER, 1);
  SPSA_INT_POISON(NMP_RED_DIVISER, 1);
  SPSA_INT_POISON(NMP_RED_MIN, 1);
  SPSA_INT_POISON(IIR_DEPTH, 1);
  SPSA_INT_FUNC(SEE_QUIET, init_reductions, 1);
  SPSA_INT_FUNC(SEE_CAPTURE, init_reductions, 1);
  SPSA_INT_POISON(SEE_DEPTH, 1);
  SPSA_INT_POISON(SE_DEPTH, 1);
  SPSA_INT(SE_DEPTH_REDUCTION, 1);
  SPSA_INT_POISON(SE_PV_DOUBLE_MARGIN, 1);
  SPSA_INT_POISON(SE_TRIPLE_MARGIN, 1);
  SPSA_INT(LMR_PV_NODE, 1);
  SPSA_INT(LMR_HISTORY_QUIET, 1);
  SPSA_INT(LMR_HISTORY_NOISY, 1);
  SPSA_INT(LMR_IN_CHECK, 1);
  SPSA_INT(LMR_CUTNODE, 1);
  SPSA_INT(LMR_TT_DEPTH, 1);
  SPSA_INT(LMR_TT_PV, 1);
  SPSA_INT(ASP_WINDOW, 1);
  SPSA_INT(ASP_DEPTH, 0);
  SPSA_INT(QS_SEE_THRESHOLD, 1);
  SPSA_INT(MO_SEE_THRESHOLD, 1);
  SPSA_INT(LMR_QUIET_HIST_DIV, 1);
  SPSA_INT(LMR_CAPT_HIST_DIV, 1);
  SPSA_INT(CAPTURE_HISTORY_BONUS_MAX, 1);
  SPSA_INT(QUIET_HISTORY_BONUS_MAX, 1);
  SPSA_INT(CONT_HISTORY_BONUS_MAX, 1);
  SPSA_INT(CAPTURE_HISTORY_MALUS_MAX, 1);
  SPSA_INT(QUIET_HISTORY_MALUS_MAX, 1);
  SPSA_INT(CONT_HISTORY_MALUS_MAX, 1);
  SPSA_INT(CAPTURE_HISTORY_BONUS_MIN, 1);
  SPSA_INT(QUIET_HISTORY_BONUS_MIN, 1);
  SPSA_INT(CONT_HISTORY_BONUS_MIN, 1);
  SPSA_INT(CAPTURE_HISTORY_MALUS_MIN, 1);
  SPSA_INT(QUIET_HISTORY_MALUS_MIN, 1);
  SPSA_INT(CONT_HISTORY_MALUS_MIN, 1);
  SPSA_INT(CORR_HISTORY_MINMAX, 1);
  SPSA_INT(PAWN_CORR_HISTORY_MULTIPLIER, 1);
  SPSA_INT(HISTORY_MAX, 0);
  SPSA_INT_NAME("SEE_PAWN", SEEPieceValues[PAWN], 1);
  SPSA_INT_NAME("SEE_KNIGHT", SEEPieceValues[KNIGHT], 1);
  SPSA_INT_NAME("SEE_BISHOP", SEEPieceValues[BISHOP], 1);
  SPSA_INT_NAME("SEE_ROOK", SEEPieceValues[ROOK], 1);
  SPSA_INT_NAME("SEE_QUEEN", SEEPieceValues[QUEEN], 1);
  SPSA_INT_NAME("MVV_PAWN", mvv[PAWN], 1);
  SPSA_INT_NAME("MVV_KNIGHT", mvv[KNIGHT], 1);
  SPSA_INT_NAME("MVV_BISHOP", mvv[BISHOP], 1);
  SPSA_INT_NAME("MVV_ROOK", mvv[ROOK], 1);
  SPSA_INT_NAME("MVV_QUEEN", mvv[QUEEN], 1);
  add_double_spsa(STRINGIFY(ASP_MULTIPLIER), &ASP_MULTIPLIER, 1,
                  SPSA_MAX(ASP_MULTIPLIER), RATE_DOUBLE(ASP_MULTIPLIER), NULL,
                  1);
  add_double_spsa(STRINGIFY(LMR_OFFSET_QUIET), &LMR_OFFSET_QUIET, 0.1,
                  SPSA_MAX(LMR_OFFSET_QUIET), RATE_DOUBLE(LMR_OFFSET_QUIET),
                  init_reductions, 1);
  add_double_spsa(STRINGIFY(LMR_DIVISOR_QUIET), &LMR_DIVISOR_QUIET, 1,
                  SPSA_MAX(LMR_DIVISOR_QUIET), RATE_DOUBLE(LMR_DIVISOR_QUIET),
                  init_reductions, 1);
  add_double_spsa(STRINGIFY(LMR_OFFSET_NOISY), &LMR_OFFSET_NOISY, -1,
                  fabs(LMR_OFFSET_NOISY), RATE_DOUBLE(LMR_OFFSET_NOISY),
                  init_reductions, 1);
  add_double_spsa(STRINGIFY(LMR_DIVISOR_NOISY), &LMR_DIVISOR_NOISY, 1,
                  SPSA_MAX(LMR_DIVISOR_NOISY), RATE_DOUBLE(LMR_DIVISOR_NOISY),
                  init_reductions, 1);
  // TM
  add_double_spsa(STRINGIFY(DEF_TIME_MULTIPLIER), &DEF_TIME_MULTIPLIER, 0,
                  SPSA_MAX(DEF_TIME_MULTIPLIER),
                  RATE_DOUBLE_TIME(DEF_TIME_MULTIPLIER), NULL, 1);
  add_double_spsa(STRINGIFY(DEF_INC_MULTIPLIER), &DEF_INC_MULTIPLIER, 0,
                  SPSA_MAX(DEF_INC_MULTIPLIER),
                  RATE_DOUBLE_TIME(DEF_INC_MULTIPLIER), NULL, 1);
  add_double_spsa(STRINGIFY(MAX_TIME_MULTIPLIER), &MAX_TIME_MULTIPLIER, 0,
                  SPSA_MAX(MAX_TIME_MULTIPLIER),
                  RATE_DOUBLE_TIME(MAX_TIME_MULTIPLIER), NULL, 1);
  add_double_spsa(STRINGIFY(HARD_LIMIT_MULTIPLIER), &HARD_LIMIT_MULTIPLIER, 1,
                  SPSA_MAX(HARD_LIMIT_MULTIPLIER),
                  RATE_DOUBLE_TIME(HARD_LIMIT_MULTIPLIER), NULL, 1);
  add_double_spsa(STRINGIFY(SOFT_LIMIT_MULTIPLIER), &SOFT_LIMIT_MULTIPLIER, 0,
                  SPSA_MAX(SOFT_LIMIT_MULTIPLIER),
                  RATE_DOUBLE_TIME(SOFT_LIMIT_MULTIPLIER), NULL, 1);
  add_double_spsa(STRINGIFY(NODE_TIME_MULTIPLIER), &NODE_TIME_MULTIPLIER, 0,
                  SPSA_MAX(NODE_TIME_MULTIPLIER),
                  RATE_DOUBLE_TIME(NODE_TIME_MULTIPLIER), NULL, 1);
  add_double_spsa(STRINGIFY(NODE_TIME_ADDITION), &NODE_TIME_ADDITION, 0,
                  SPSA_MAX(NODE_TIME_ADDITION),
                  RATE_DOUBLE_TIME(NODE_TIME_ADDITION), NULL, 1);
  add_double_spsa(STRINGIFY(NODE_TIME_MIN), &NODE_TIME_MIN, 0,
                  SPSA_MAX(NODE_TIME_MIN), RATE_DOUBLE_TIME(NODE_TIME_MIN),
                  NULL, 1);

  add_double_spsa("BESTMOVE_SCALE0", &bestmove_scale[0], 0,
                  SPSA_MAX(bestmove_scale[0]),
                  RATE_DOUBLE_TIME(bestmove_scale[0]), NULL, 1);
  add_double_spsa("BESTMOVE_SCALE1", &bestmove_scale[1], 0,
                  SPSA_MAX(bestmove_scale[1]),
                  RATE_DOUBLE_TIME(bestmove_scale[1]), NULL, 1);
  add_double_spsa("BESTMOVE_SCALE2", &bestmove_scale[2], 0,
                  SPSA_MAX(bestmove_scale[2]),
                  RATE_DOUBLE_TIME(bestmove_scale[2]), NULL, 1);
  add_double_spsa("BESTMOVE_SCALE3", &bestmove_scale[3], 0,
                  SPSA_MAX(bestmove_scale[3]),
                  RATE_DOUBLE_TIME(bestmove_scale[3]), NULL, 1);
  add_double_spsa("BESTMOVE_SCALE4", &bestmove_scale[4], 0,
                  SPSA_MAX(bestmove_scale[4]),
                  RATE_DOUBLE_TIME(bestmove_scale[4]), NULL, 1);

  add_double_spsa("EVAL_SCALE0", &eval_scale[0], 0, SPSA_MAX(eval_scale[0]),
                  RATE_DOUBLE_TIME(eval_scale[0]), NULL, 1);
  add_double_spsa("EVAL_SCALE1", &eval_scale[1], 0, SPSA_MAX(eval_scale[1]),
                  RATE_DOUBLE_TIME(eval_scale[1]), NULL, 1);
  add_double_spsa("EVAL_SCALE2", &eval_scale[2], 0, SPSA_MAX(eval_scale[2]),
                  RATE_DOUBLE_TIME(eval_scale[2]), NULL, 1);
  add_double_spsa("EVAL_SCALE3", &eval_scale[3], 0, SPSA_MAX(eval_scale[3]),
                  RATE_DOUBLE_TIME(eval_scale[3]), NULL, 1);
  add_double_spsa("EVAL_SCALE4", &eval_scale[4], 0, SPSA_MAX(eval_scale[4]),
                  RATE_DOUBLE_TIME(eval_scale[4]), NULL, 1);
}

void print_spsa_table_uci(void) {
  for (int i = 0; i < spsa_index; i++) {
    if (!spsa[i].tunable) {
      continue;
    }
    if (spsa[i].is_float) {
      printf("option name %s type string default %lf\n", spsa[i].name,
             *(double *)spsa[i].value);
    } else {
      printf("option name %s type spin default %d min %" PRIu64 " "
             "max %" PRIu64 "\n",
             spsa[i].name, *(int *)spsa[i].value, spsa[i].min.min_int,
             spsa[i].max.max_int);
    }
  }
}

void print_spsa_table(void) {
  for (int i = 0; i < spsa_index; i++) {
    if (!spsa[i].tunable) {
      continue;
    }
    if (spsa[i].is_float) {
      printf("%s, float, %lf, %lf, %lf, %lf, 0.002\n", spsa[i].name,
             *(double *)spsa[i].value, spsa[i].min.min_float,
             spsa[i].max.max_float, spsa[i].rate);
    } else {
      printf("%s, int, %f, %f, %f, %lf, 0.002\n", spsa[i].name,
             (float)*(int *)spsa[i].value, (float)spsa[i].min.min_int,
             (float)spsa[i].max.max_int, spsa[i].rate);
    }
  }
}

void handle_spsa_change(char input[10000]) {
  for (int i = 0; i < spsa_index; i++) {
    char option[100] = "setoption name ";
    strcat(option, spsa[i].name);
    strcat(option, " value ");
    if (!strncmp(input, option, strlen(option))) {
      if (spsa[i].is_float) {
        sscanf(input, "%*s %*s %*s %*s %lf", (double *)spsa[i].value);
      } else {
        sscanf(input, "%*s %*s %*s %*s %d", (int *)spsa[i].value);
      }
      if (spsa[i].func != NULL) {
        spsa[i].func();
      }
    }
  }
}
