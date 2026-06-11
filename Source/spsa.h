#ifndef SPSA_H
#define SPSA_H

#include <stdint.h>

#ifdef TUNE
#define TUNABLE(...) __VA_ARGS__
#else
#define TUNABLE(...) const __VA_ARGS__
#endif

void init_spsa_table(void);
void print_spsa_table_uci(void);
void print_spsa_table(void);
void handle_spsa_change(char input[10000]);

#endif
