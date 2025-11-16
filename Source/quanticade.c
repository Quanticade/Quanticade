// system headers
#include "evaluate.h"
#include "move.h"
#include "movegen.h"
#include "nnue.h"
#include "search.h"
#include "spsa.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef WIN64
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "attacks.h"
#include "enums.h"
#include "structs.h"
#include "transposition.h"
#include "uci.h"

position_t pos;
nnue_settings_t nnue_settings;
limits_t limits;
keys_t keys;

extern const int default_hash_size;
extern int thread_count;
extern nnue_t nnue;
extern uint64_t between[64][64];
extern uint64_t line[64][64];

// SplitMix64 PRNG for generating random hash keys
uint64_t sm64_state;
uint64_t get_random_uint64_number(void) {
  uint64_t z = (sm64_state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

// init random hash keys (zobrist keys)
static inline void init_random_keys(void) {
  // loop over piece codes
  for (int piece = P; piece <= k; piece++) {
    // loop over board squares
    for (int square = 0; square < 64; square++)
      // init random piece keys
      keys.piece_keys[piece][square] = get_random_uint64_number();
  }

  // loop over board squares
  for (int square = 0; square < 64; square++)
    // init random enpassant keys
    keys.enpassant_keys[square] = get_random_uint64_number();

  // loop over castling keys
  for (int index = 0; index < 16; index++)
    // init castling keys
    keys.castle_keys[index] = get_random_uint64_number();

  // init random side key
  keys.side_key = get_random_uint64_number();
}

// init all variables
void init_all(void) {
  // init leaper pieces attacks
  init_leapers_attacks();

  // init slider pieces attacks
  init_sliders_attacks();

  // init random keys for hashing purposes
  init_random_keys();

  init_reductions();

  init_spsa_table();

  init_between_bitboards();

  // init hash table with default size
  init_hash_table(default_hash_size);

  nnue_init("hati.nnue");
}

/**********************************\
 ==================================

             Main driver

 ==================================
\**********************************/

int main(int argc, char *argv[]) {
  pos.enpassant = no_sq;
  limits.movestogo = 30;
  limits.time = -1;
  tt.hash_entry = NULL;
  tt.num_of_entries = 0;
  nnue_settings.nnue_file = calloc(21, 1);
  strcpy(nnue_settings.nnue_file, "hati.nnue");
  // init all
  init_all();

  // connect to GUI
  uci_loop(&pos, argc, argv);

  // free hash table memory on exit
  free(tt.hash_entry);
  free(nnue_settings.nnue_file);

  return 0;
}
