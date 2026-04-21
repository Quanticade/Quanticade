#include "utils.h"
#include "bitboards.h"
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef WIN64
#include <windows.h>
#else
#include <sys/time.h>
#endif

// Misc functions. Some of them from VICE by Richard Allbert

int clamp(int d, int min, int max) {
  const int t = d < min ? min : d;
  return t > max ? max : t;
}

uint64_t get_time_ms(void) {
#ifdef WIN64
  return GetTickCount64();
#else
  struct timeval time_value;
  gettimeofday(&time_value, NULL);
  return (uint64_t)time_value.tv_sec * 1000 + time_value.tv_usec / 1000;
#endif
}

uint8_t is_win(int16_t score) {
  return score > MATE_SCORE;
}

uint8_t is_loss(int16_t score) {
  return score < -MATE_SCORE;
}

uint8_t is_decisive(int16_t score) {
  return abs(score) > MATE_SCORE;
}
