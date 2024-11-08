#include "spsa.h"
#include "enums.h"
#include "search.h"
#include "structs.h"
#include "uci.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

spsa_t spsa[100];

uint8_t spsa_index = 0;

extern int LMP_BASE;
extern int LMP_MULTIPLIER;
extern int RAZOR_DEPTH;
extern int RAZOR_MARGIN;
extern int RFP_DEPTH;
extern int RFP_MARGIN;
extern int RFP_BONUS;
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
extern int ASP_WINDOW;
extern int ASP_DEPTH;
extern int QS_SEE_THRESHOLD;
extern int MO_SEE_THRESHOLD;
extern int CAPTURE_HISTORY_BONUS_MAX;
extern int QUIET_HISTORY_BONUS_MAX;
extern int CAPTURE_HISTORY_MALUS_MAX;
extern int QUIET_HISTORY_MALUS_MAX;
extern int HISTORY_MAX;
extern int HP_ADDITION;
extern int HP_MULTIPLIER;
extern int HP_DEPTH;
extern double ASP_MULTIPLIER;
extern int LMR_HIST_DIV;
extern double LMR_OFFSET_NOISY;
extern double LMR_DIVISOR_NOISY;
extern double LMR_OFFSET_QUIET;
extern double LMR_DIVISOR_QUIET;

extern int SEEPieceValues[];

#define RATE(VALUE) MAX(0.5, MAX(1, (((float)VALUE * 2) - 1)) / 20)
#define RATE_DOUBLE(VALUE) MAX(0.05, MAX(1, ((VALUE * 2) - 1)) / 20)
#define SPSA_MAX(VALUE) VALUE * 2
#define STRINGIFY(VARIABLE) (#VARIABLE)

void add_double_spsa(char name[], double *value, double min, double max,
                     double rate, void (*func)(void)) {
  strcpy(spsa[spsa_index].name, name);
  spsa[spsa_index].value = value;
  spsa[spsa_index].min.min_float = min;
  spsa[spsa_index].max.max_float = max;
  spsa[spsa_index].rate = rate;
  spsa[spsa_index].is_float = 1;
  spsa[spsa_index].func = func;
  spsa_index++;
}

void add_int_spsa(char name[], int *value, int min, int max, double rate,
                  void (*func)(void)) {
  strcpy(spsa[spsa_index].name, name);
  spsa[spsa_index].value = value;
  spsa[spsa_index].min.min_int = min;
  spsa[spsa_index].max.max_int = max;
  spsa[spsa_index].rate = rate;
  spsa[spsa_index].is_float = 0;
  spsa[spsa_index].func = func;
  spsa_index++;
}

#define SPSA_INT(VARIABLE)                                                     \
  add_int_spsa(STRINGIFY(VARIABLE), &VARIABLE, 1, SPSA_MAX(VARIABLE),          \
               RATE(VARIABLE), NULL)
#define SPSA_INT_NAME(NAME, VARIABLE)                                          \
  add_int_spsa(NAME, &VARIABLE, 1, SPSA_MAX(VARIABLE), RATE(VARIABLE), NULL)

void init_spsa_table(void) {
  SPSA_INT(LMP_BASE);
  SPSA_INT(LMP_MULTIPLIER);
  SPSA_INT(RAZOR_DEPTH);
  SPSA_INT(RAZOR_MARGIN);
  SPSA_INT(RFP_DEPTH);
  SPSA_INT(RFP_MARGIN);
  SPSA_INT(RFP_BONUS);
  SPSA_INT(FP_DEPTH);
  SPSA_INT(FP_MULTIPLIER);
  SPSA_INT(FP_ADDITION);
  SPSA_INT(NMP_BASE_REDUCTION);
  SPSA_INT(NMP_DIVISER);
  SPSA_INT(NMP_RED_DIVISER);
  SPSA_INT(NMP_RED_MIN);
  SPSA_INT(IIR_DEPTH);
  SPSA_INT(SEE_QUIET);
  SPSA_INT(SEE_CAPTURE);
  SPSA_INT(SEE_DEPTH);
  SPSA_INT(SE_DEPTH);
  SPSA_INT(SE_DEPTH_REDUCTION);
  SPSA_INT(ASP_WINDOW);
  SPSA_INT(ASP_DEPTH);
  SPSA_INT(QS_SEE_THRESHOLD);
  SPSA_INT(MO_SEE_THRESHOLD);
  SPSA_INT(CAPTURE_HISTORY_BONUS_MAX);
  SPSA_INT(QUIET_HISTORY_BONUS_MAX);
  SPSA_INT(CAPTURE_HISTORY_MALUS_MAX);
  SPSA_INT(QUIET_HISTORY_MALUS_MAX);
  SPSA_INT(HP_ADDITION);
  SPSA_INT(HP_MULTIPLIER);
  SPSA_INT(HP_DEPTH);
  SPSA_INT_NAME("SEE_PAWN", SEEPieceValues[PAWN]);
  SPSA_INT_NAME("SEE_KNIGHT", SEEPieceValues[KNIGHT]);
  SPSA_INT_NAME("SEE_BISHOP", SEEPieceValues[BISHOP]);
  SPSA_INT_NAME("SEE_ROOK", SEEPieceValues[ROOK]);
  SPSA_INT_NAME("SEE_QUEEN", SEEPieceValues[QUEEN]);
  add_double_spsa(STRINGIFY(ASP_MULTIPLIER), &ASP_MULTIPLIER, 1,
                  SPSA_MAX(ASP_MULTIPLIER), RATE_DOUBLE(ASP_MULTIPLIER), NULL);
  SPSA_INT(LMR_HIST_DIV);
  add_double_spsa(STRINGIFY(LMR_OFFSET_QUIET), &LMR_OFFSET_QUIET, 0.1,
                  SPSA_MAX(LMR_OFFSET_QUIET), RATE_DOUBLE(LMR_OFFSET_QUIET),
                  init_reductions);
  add_double_spsa(STRINGIFY(LMR_DIVISOR_QUIET), &LMR_DIVISOR_QUIET, 1,
                  SPSA_MAX(LMR_DIVISOR_QUIET), RATE_DOUBLE(LMR_DIVISOR_QUIET),
                  init_reductions);
  add_double_spsa(STRINGIFY(LMR_OFFSET_NOISY), &LMR_OFFSET_NOISY, -1,
                  fabs(LMR_OFFSET_NOISY), RATE_DOUBLE(LMR_OFFSET_NOISY),
                  init_reductions);
  add_double_spsa(STRINGIFY(LMR_DIVISOR_NOISY), &LMR_DIVISOR_NOISY, 1,
                  SPSA_MAX(LMR_DIVISOR_NOISY), RATE_DOUBLE(LMR_DIVISOR_NOISY),
                  init_reductions);
}

void print_spsa_table_uci(void) {
  for (int i = 0; i < spsa_index; i++) {
    if (spsa[i].is_float) {
      printf("option name %s type string default %lf\n", spsa[i].name,
             *(double *)spsa[i].value);
    } else {
      printf("option name %s type spin default %d min %lu max %lu\n",
             spsa[i].name, *(int *)spsa[i].value, spsa[i].min.min_int,
             spsa[i].max.max_int);
    }
  }
}

void print_spsa_table(void) {
  for (int i = 0; i < spsa_index; i++) {
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
