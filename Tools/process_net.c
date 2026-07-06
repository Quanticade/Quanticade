#include "../Source/arch.h"
#include "../Source/nnue.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct raw_net {
  float feature_weights[KING_BUCKETS][PSQT_FEATURES][L1_SIZE];
  float feature_threats[THREAT_FEATURES][L1_SIZE];
  float feature_bias[L1_SIZE];
  float l1_weights[OUTPUT_BUCKETS][L2_SIZE][L1_SIZE];
  float l1_bias[OUTPUT_BUCKETS][L2_SIZE];
  float l2_weights[OUTPUT_BUCKETS][L3_SIZE][2 * L2_SIZE];
  float l2_bias[OUTPUT_BUCKETS][L3_SIZE];
  float l3_weights[OUTPUT_BUCKETS][L3_SIZE];
  float l3_bias[OUTPUT_BUCKETS];
};

const int INT8_PER_INT32 = sizeof(int) / sizeof(int8_t);

int main(int argc, char *argv[]) {
  (void)argc;

  struct raw_net *raw = malloc(sizeof(struct raw_net));
  nnue_t *processed = malloc(sizeof(nnue_t));

  FILE *in = fopen(argv[1], "rb");
  int size = fread(raw, sizeof(struct raw_net), 1, in);
  if (size != 1) {
    fclose(in);
    return -1;
  }
  fclose(in);

#if defined(USE_SIMD)
  for (int b = 0; b < OUTPUT_BUCKETS; b++) {
    for (int l1 = 0; l1 < L1_SIZE / INT8_PER_INT32; l1++) {
      for (int l2 = 0; l2 < L2_SIZE; l2++) {
        for (int c = 0; c < INT8_PER_INT32; c++) {
          processed->l1_weights[b][l1 * INT8_PER_INT32 * L2_SIZE +
                                   l2 * INT8_PER_INT32 + c] =
              round(raw->l1_weights[b][l2][l1 * INT8_PER_INT32 + c] * L1_QUANT);
        }
      }
    }
  }
#else
  for (int b = 0; b < OUTPUT_BUCKETS; b++) {
    for (int l1 = 0; l1 < L1_SIZE; l1++) {
      for (int l2 = 0; l2 < L2_SIZE; l2++) {
        processed->l1_weights[b][l1 * L2_SIZE + l2] =
            round(raw->l1_weights[b][l2][l1] * L1_QUANT);
      }
    }
  }
#endif
  for (int b = 0; b < KING_BUCKETS; b++) {
    for (int input = 0; input < PSQT_FEATURES; input++) {
      for (int l1 = 0; l1 < L1_SIZE; l1++) {
        processed->feature_weights[b][input][l1] =
            round(raw->feature_weights[b][input][l1] * INPUT_QUANT);
      }
    }
  }
  for (int t = 0; t < THREAT_FEATURES; t++) {
    for (int l1 = 0; l1 < L1_SIZE; l1++) {
      float q = round(raw->feature_threats[t][l1] * INPUT_QUANT);
      q = q < -128 ? -128 : q > 127 ? 127 : q;
      processed->feature_threats[t][l1] = (int8_t)q;
    }
  }
  for (int l1 = 0; l1 < L1_SIZE; l1++) {
    processed->feature_bias[l1] = round(raw->feature_bias[l1] * INPUT_QUANT);
  }
  for (int b = 0; b < OUTPUT_BUCKETS; b++) {
    for (int l2 = 0; l2 < 2 * L2_SIZE; l2++) {
      for (int l3 = 0; l3 < L3_SIZE; l3++) {
        processed->l2_weights[b][l2][l3] = raw->l2_weights[b][l3][l2];
      }
    }
  }
  for (int b = 0; b < OUTPUT_BUCKETS; b++) {
    for (int l3 = 0; l3 < L3_SIZE; l3++) {
      processed->l3_weights[b][l3] = raw->l3_weights[b][l3];
    }
  }
  memcpy(processed->l1_bias, raw->l1_bias, sizeof(raw->l1_bias));
  memcpy(processed->l2_bias, raw->l2_bias, sizeof(raw->l2_bias));
  memcpy(processed->l3_bias, raw->l3_bias, sizeof(raw->l3_bias));

  FILE *out = fopen(argv[2], "wb");
  fwrite(processed, sizeof(nnue_t), 1, out);
  fclose(out);

  free(raw);
  free(processed);

  return 0;
}
