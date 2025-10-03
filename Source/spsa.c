#include "spsa.h"
#include "enums.h"
#include "search.h"
#include "structs.h"
#include "uci.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

spsa_t spsa[500];

uint8_t spsa_index = 0;

// search.c
extern int RAZOR_DEPTH;
extern int RAZOR_MARGIN;
extern int RFP_DEPTH;
extern int RFP_MARGIN;
extern int RFP_BASE_MARGIN;
extern int RFP_IMPROVING;
extern int RFP_OPP_WORSENING;
extern int FP_DEPTH;
extern int FP_MULTIPLIER;
extern int FP_ADDITION;
extern int FP_HISTORY_DIVISOR;
extern int NMP_BASE_REDUCTION;
extern int NMP_DIVISER;
extern int NMP_RED_DIVISER;
extern int NMP_RED_MIN;
extern int NMP_BASE_ADD;
extern int NMP_MULTIPLIER;
extern int IIR_DEPTH;
extern int IIR_DEPTH_REDUCTION;
extern int SEE_QUIET;
extern int SEE_CAPTURE;
extern int SEE_DEPTH;
extern int SEE_HISTORY_DIVISOR;
extern int SE_DEPTH;
extern int SE_DEPTH_REDUCTION;
extern int SE_PV_DOUBLE_MARGIN;
extern int SE_TRIPLE_MARGIN;
extern int LMR_PV_NODE;
extern int LMR_HISTORY_QUIET;
extern int LMR_HISTORY_NOISY;
extern int LMR_WAS_IN_CHECK;
extern int LMR_IN_CHECK;
extern int LMR_CUTNODE;
extern int LMR_TT_DEPTH;
extern int LMR_TT_PV;
extern int LMR_TT_PV_CUTNODE;
extern int LMR_TT_SCORE;
extern int LMR_CUTOFF_CNT;
extern int LMR_IMPROVING;
extern int LMR_DEEPER_MARGIN;
extern int LMR_SHALLOWER_MARGIN;
extern int LMP_DEPTH_DIVISOR;
extern int ASP_WINDOW;
extern int ASP_DEPTH;
extern int QS_SEE_THRESHOLD;
extern int QS_FUTILITY_THRESHOLD;
extern int MO_SEE_THRESHOLD;
extern int LMR_QUIET_HIST_DIV;
extern int LMR_CAPT_HIST_DIV;
extern int ASP_WINDOW_DIVISER;
extern int ASP_WINDOW_MULTIPLIER;
extern int EVAL_STABILITY_VAR;
extern int HINDSIGH_REDUCTION_ADD;
extern int HINDSIGH_REDUCTION_RED;
extern int HINDSIGN_REDUCTION_EVAL_MARGIN;
extern int PROBCUT_MARGIN;
extern int PROBCUT_SHALLOW_DEPTH;
extern int PROBCUT_SEE_THRESHOLD;
extern int MO_QUIET_HIST_MULT;
extern int MO_CONT1_HIST_MULT;
extern int MO_CONT2_HIST_MULT;
extern int MO_CONT4_HIST_MULT;
extern int MO_PAWN_HIST_MULT;
extern int MO_CAPT_HIST_MULT;
extern int MO_MVV_MULT;
extern int SEARCH_QUIET_HIST_MULT;
extern int SEARCH_CONT1_HIST_MULT;
extern int SEARCH_CONT2_HIST_MULT;
extern int SEARCH_PAWN_HIST_MULT;
extern int SEARCH_CAPT_HIST_MULT;
extern int SEARCH_MVV_MULT;

extern double LMR_DEEPER_MULT;

extern double LMP_MARGIN_WORSENING_BASE;
extern double LMP_MARGIN_WORSENING_FACTOR;
extern double LMP_MARGIN_WORSENING_POWER;
extern double LMP_MARGIN_IMPROVING_BASE;
extern double LMP_MARGIN_IMPROVING_FACTOR;
extern double LMP_MARGIN_IMPROVING_POWER;

extern double LMR_OFFSET_QUIET;
extern double LMR_DIVISOR_QUIET;
extern double LMR_OFFSET_NOISY;
extern double LMR_DIVISOR_NOISY;

// history.c
extern int QUIET_HISTORY_MALUS_MAX;
extern int QUIET_HISTORY_BONUS_MAX;
extern int QUIET_HISTORY_BASE_BONUS;
extern int QUIET_HISTORY_FACTOR_BONUS;
extern int QUIET_HISTORY_BASE_MALUS;
extern int QUIET_HISTORY_FACTOR_MALUS;
extern int QUIET_HISTORY_MAX_TT;
extern int QUIET_HISTORY_TT_FACTOR;
extern int QUIET_HISTORY_TT_BASE;

extern int CAPTURE_HISTORY_MALUS_MAX;
extern int CAPTURE_HISTORY_BONUS_MAX;
extern int CAPTURE_HISTORY_BASE_BONUS;
extern int CAPTURE_HISTORY_FACTOR_BONUS;
extern int CAPTURE_HISTORY_BASE_MALUS;
extern int CAPTURE_HISTORY_FACTOR_MALUS;

extern int CONT_HISTORY_MALUS_MAX;
extern int CONT_HISTORY_BONUS_MAX;
extern int CONT_HISTORY_BASE_BONUS;
extern int CONT_HISTORY_FACTOR_BONUS;
extern int CONT_HISTORY_BASE_MALUS;
extern int CONT_HISTORY_FACTOR_MALUS;
extern int CONT_HISTORY_BASE2_BONUS;
extern int CONT_HISTORY_FACTOR2_BONUS;
extern int CONT_HISTORY_BASE2_MALUS;
extern int CONT_HISTORY_FACTOR2_MALUS;
extern int CONT_HISTORY_BASE4_BONUS;
extern int CONT_HISTORY_FACTOR4_BONUS;
extern int CONT_HISTORY_BASE4_MALUS;
extern int CONT_HISTORY_FACTOR4_MALUS;

extern int PAWN_HISTORY_MALUS_MAX;
extern int PAWN_HISTORY_BONUS_MAX;
extern int PAWN_HISTORY_BASE_BONUS;
extern int PAWN_HISTORY_FACTOR_BONUS;
extern int PAWN_HISTORY_BASE_MALUS;
extern int PAWN_HISTORY_FACTOR_MALUS;
extern int CORR_HISTORY_MINMAX;
extern int PAWN_CORR_HISTORY_MULTIPLIER;
extern int NON_PAWN_CORR_HISTORY_MULTIPLIER;
extern int FIFTY_MOVE_SCALING;
extern int HISTORY_MAX;

// TM
extern double DEF_TIME_MULTIPLIER;
extern double DEF_INC_MULTIPLIER;
extern double MAX_TIME_MULTIPLIER;
extern double SOFT_LIMIT_MULTIPLIER;
extern double NODE_TIME_MULTIPLIER;
extern double NODE_TIME_ADDITION;
extern double NODE_TIME_MIN;

extern int mvv[];
extern int SEEPieceValues[];
extern int EVAL_KNIGHT;
extern int EVAL_BISHOP;
extern int EVAL_ROOK;
extern int EVAL_QUEEN;
extern int EVAL_SCALE_BASE;

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
#define SPSA_INT_MINMAX(VARIABLE, TUNABLE, MINIMUM, MAXIMUM)                   \
  add_int_spsa(STRINGIFY(VARIABLE), &VARIABLE, MINIMUM, MAXIMUM,               \
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
  SPSA_INT_POISON(RAZOR_DEPTH, 0);
  SPSA_INT(RAZOR_MARGIN, 1);
  SPSA_INT_POISON(RFP_DEPTH, 0);
  SPSA_INT(RFP_MARGIN, 1);
  SPSA_INT(RFP_BASE_MARGIN, 1);
  SPSA_INT(RFP_IMPROVING, 1);
  SPSA_INT(RFP_OPP_WORSENING, 1);
  SPSA_INT_POISON(FP_DEPTH, 0);
  SPSA_INT(FP_MULTIPLIER, 1);
  SPSA_INT(FP_ADDITION, 1);
  SPSA_INT(FP_HISTORY_DIVISOR, 1);
  SPSA_INT_POISON(NMP_BASE_REDUCTION, 0);
  SPSA_INT(NMP_DIVISER, 0);
  SPSA_INT(NMP_RED_DIVISER, 1);
  SPSA_INT_POISON(NMP_RED_MIN, 0);
  SPSA_INT(NMP_BASE_ADD, 1);
  SPSA_INT(NMP_MULTIPLIER, 1);
  SPSA_INT_POISON(IIR_DEPTH, 0);
  SPSA_INT_POISON(IIR_DEPTH_REDUCTION, 1);
  SPSA_INT_FUNC(SEE_QUIET, init_reductions, 1);
  SPSA_INT_FUNC(SEE_CAPTURE, init_reductions, 1);
  SPSA_INT_POISON(SEE_DEPTH, 0);
  SPSA_INT(SEE_HISTORY_DIVISOR, 1);
  SPSA_INT_POISON(SE_DEPTH, 0);
  SPSA_INT(SE_DEPTH_REDUCTION, 1);
  SPSA_INT_POISON(SE_PV_DOUBLE_MARGIN, 1);
  SPSA_INT_POISON(SE_TRIPLE_MARGIN, 1);
  SPSA_INT(LMR_PV_NODE, 1);
  SPSA_INT(LMR_HISTORY_QUIET, 1);
  SPSA_INT(LMR_HISTORY_NOISY, 1);
  SPSA_INT(LMR_WAS_IN_CHECK, 1);
  SPSA_INT(LMR_IN_CHECK, 1);
  SPSA_INT(LMR_CUTNODE, 1);
  SPSA_INT(LMR_TT_DEPTH, 1);
  SPSA_INT(LMR_TT_PV, 1);
  SPSA_INT(LMR_TT_PV_CUTNODE, 1);
  SPSA_INT(LMR_TT_SCORE, 1);
  SPSA_INT(LMR_CUTOFF_CNT, 1);
  SPSA_INT(LMR_IMPROVING, 1);
  SPSA_INT(LMR_DEEPER_MARGIN, 1);
  SPSA_INT(LMR_SHALLOWER_MARGIN, 1);
  SPSA_INT(LMP_DEPTH_DIVISOR, 1);
  SPSA_INT(ASP_WINDOW, 1);
  SPSA_INT(ASP_DEPTH, 0);
  SPSA_INT(QS_SEE_THRESHOLD, 1);
  SPSA_INT(QS_FUTILITY_THRESHOLD, 1);
  SPSA_INT(MO_SEE_THRESHOLD, 1);
  SPSA_INT(LMR_QUIET_HIST_DIV, 1);
  SPSA_INT(LMR_CAPT_HIST_DIV, 1);
  SPSA_INT(ASP_WINDOW_DIVISER, 1);
  SPSA_INT(ASP_WINDOW_MULTIPLIER, 1);
  SPSA_INT(EVAL_STABILITY_VAR, 1);
  SPSA_INT(HINDSIGH_REDUCTION_ADD, 1);
  SPSA_INT(HINDSIGH_REDUCTION_RED, 1);
  SPSA_INT(HINDSIGN_REDUCTION_EVAL_MARGIN, 1);
  SPSA_INT(PROBCUT_MARGIN, 1);
  SPSA_INT(PROBCUT_SHALLOW_DEPTH, 1);
  SPSA_INT(PROBCUT_SEE_THRESHOLD, 1);
  SPSA_INT(MO_QUIET_HIST_MULT, 1);
  SPSA_INT(MO_CONT1_HIST_MULT, 1);
  SPSA_INT(MO_CONT2_HIST_MULT, 1);
  SPSA_INT(MO_CONT4_HIST_MULT, 1);
  SPSA_INT(MO_PAWN_HIST_MULT, 1);
  SPSA_INT(MO_CAPT_HIST_MULT, 1);
  SPSA_INT(MO_MVV_MULT, 1);
  SPSA_INT(SEARCH_QUIET_HIST_MULT, 1);
  SPSA_INT(SEARCH_CONT1_HIST_MULT, 1);
  SPSA_INT(SEARCH_CONT2_HIST_MULT, 1);
  SPSA_INT(SEARCH_PAWN_HIST_MULT, 1);
  SPSA_INT(SEARCH_CAPT_HIST_MULT, 1);
  SPSA_INT(SEARCH_MVV_MULT, 1);

  add_double_spsa(STRINGIFY(LMR_DEEPER_MULT), &LMR_DEEPER_MULT, 1.0,
                  SPSA_MAX(LMR_DEEPER_MULT), RATE_DOUBLE(LMR_DEEPER_MULT), NULL,
                  1);

  SPSA_INT(QUIET_HISTORY_MALUS_MAX, 1);
  SPSA_INT(QUIET_HISTORY_BONUS_MAX, 1);
  SPSA_INT_MINMAX(QUIET_HISTORY_BASE_BONUS, 1, 0, 40);
  SPSA_INT(QUIET_HISTORY_FACTOR_BONUS, 1);
  SPSA_INT_MINMAX(QUIET_HISTORY_BASE_MALUS, 1, 0, 40);
  SPSA_INT(QUIET_HISTORY_FACTOR_MALUS, 1);
  SPSA_INT(QUIET_HISTORY_MAX_TT, 1);
  SPSA_INT(QUIET_HISTORY_TT_FACTOR, 1);
  SPSA_INT(QUIET_HISTORY_TT_BASE, 1);

  SPSA_INT(CAPTURE_HISTORY_MALUS_MAX, 1);
  SPSA_INT(CAPTURE_HISTORY_BONUS_MAX, 1);
  SPSA_INT_MINMAX(CAPTURE_HISTORY_BASE_BONUS, 1, 0, 40);
  SPSA_INT(CAPTURE_HISTORY_FACTOR_BONUS, 1);
  SPSA_INT_MINMAX(CAPTURE_HISTORY_BASE_MALUS, 1, 0, 40);
  SPSA_INT(CAPTURE_HISTORY_FACTOR_MALUS, 1);

  SPSA_INT(CONT_HISTORY_MALUS_MAX, 1);
  SPSA_INT(CONT_HISTORY_BONUS_MAX, 1);
  SPSA_INT_MINMAX(CONT_HISTORY_BASE_BONUS, 1, 0, 40);
  SPSA_INT(CONT_HISTORY_FACTOR_BONUS, 1);
  SPSA_INT_MINMAX(CONT_HISTORY_BASE_MALUS, 1, 0, 40);
  SPSA_INT(CONT_HISTORY_FACTOR_MALUS, 1);
  SPSA_INT_MINMAX(CONT_HISTORY_BASE2_BONUS, 1, 0, 40);
  SPSA_INT(CONT_HISTORY_FACTOR2_BONUS, 1);
  SPSA_INT_MINMAX(CONT_HISTORY_BASE2_MALUS, 1, 0, 40);
  SPSA_INT(CONT_HISTORY_FACTOR2_MALUS, 1);
  SPSA_INT_MINMAX(CONT_HISTORY_BASE4_BONUS, 1, 0, 40);
  SPSA_INT(CONT_HISTORY_FACTOR4_BONUS, 1);
  SPSA_INT_MINMAX(CONT_HISTORY_BASE4_MALUS, 1, 0, 40);
  SPSA_INT(CONT_HISTORY_FACTOR4_MALUS, 1);

  SPSA_INT(PAWN_HISTORY_MALUS_MAX, 1);
  SPSA_INT(PAWN_HISTORY_BONUS_MAX, 1);
  SPSA_INT_MINMAX(PAWN_HISTORY_BASE_BONUS, 1, 0, 40);
  SPSA_INT(PAWN_HISTORY_FACTOR_BONUS, 1);
  SPSA_INT_MINMAX(PAWN_HISTORY_BASE_MALUS, 1, 0, 40);
  SPSA_INT(PAWN_HISTORY_FACTOR_MALUS, 1);

  SPSA_INT(CORR_HISTORY_MINMAX, 1);
  SPSA_INT(PAWN_CORR_HISTORY_MULTIPLIER, 1);
  SPSA_INT(NON_PAWN_CORR_HISTORY_MULTIPLIER, 1);
  SPSA_INT(FIFTY_MOVE_SCALING, 1);
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
  SPSA_INT(EVAL_KNIGHT, 1);
  SPSA_INT(EVAL_BISHOP, 1);
  SPSA_INT(EVAL_ROOK, 1);
  SPSA_INT(EVAL_QUEEN, 1);
  SPSA_INT(EVAL_SCALE_BASE, 1);

  add_double_spsa(STRINGIFY(LMP_MARGIN_WORSENING_BASE),
                  &LMP_MARGIN_WORSENING_BASE, 1.0,
                  SPSA_MAX(LMP_MARGIN_WORSENING_BASE),
                  RATE_DOUBLE(LMP_MARGIN_WORSENING_BASE), init_reductions, 1);
  add_double_spsa(STRINGIFY(LMP_MARGIN_WORSENING_FACTOR),
                  &LMP_MARGIN_WORSENING_FACTOR, 0.1,
                  SPSA_MAX(LMP_MARGIN_WORSENING_FACTOR),
                  RATE_DOUBLE(LMP_MARGIN_WORSENING_FACTOR), init_reductions, 1);
  add_double_spsa(STRINGIFY(LMP_MARGIN_WORSENING_POWER),
                  &LMP_MARGIN_WORSENING_POWER, 1.0,
                  SPSA_MAX(LMP_MARGIN_WORSENING_POWER),
                  RATE_DOUBLE(LMP_MARGIN_WORSENING_POWER), init_reductions, 1);
  add_double_spsa(STRINGIFY(LMP_MARGIN_IMPROVING_BASE),
                  &LMP_MARGIN_IMPROVING_BASE, 1.0,
                  SPSA_MAX(LMP_MARGIN_IMPROVING_BASE),
                  RATE_DOUBLE(LMP_MARGIN_IMPROVING_BASE), init_reductions, 1);
  add_double_spsa(STRINGIFY(LMP_MARGIN_IMPROVING_FACTOR),
                  &LMP_MARGIN_IMPROVING_FACTOR, 0.1,
                  SPSA_MAX(LMP_MARGIN_IMPROVING_FACTOR),
                  RATE_DOUBLE(LMP_MARGIN_IMPROVING_FACTOR), init_reductions, 1);
  add_double_spsa(STRINGIFY(LMP_MARGIN_IMPROVING_POWER),
                  &LMP_MARGIN_IMPROVING_POWER, 1.0,
                  SPSA_MAX(LMP_MARGIN_IMPROVING_POWER),
                  RATE_DOUBLE(LMP_MARGIN_IMPROVING_POWER), init_reductions, 1);

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
    char option[500] = "setoption name ";
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
