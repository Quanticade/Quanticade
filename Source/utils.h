#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

int clamp(int d, int min, int max);
double dclamp(double d, double min, double max);
uint64_t get_time_ms(void);
uint8_t is_win(int16_t score);
uint8_t is_loss(int16_t score);
uint8_t is_decisive(int16_t score);

#endif
