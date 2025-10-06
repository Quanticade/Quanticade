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
#include "threads.h"
#include "transposition.h"
#include "uci.h"

position_t pos;
thread_t *threads;
nnue_settings_t nnue_settings;
limits_t limits;
keys_t keys;
uint32_t random_state;

extern const int default_hash_size;
extern int thread_count;
extern nnue_t nnue;
extern uint64_t between[64][64];

// generate 32-bit pseudo legal numbers
uint32_t get_random_U32_number(void) {
  // get current state
  uint32_t number = random_state;

  // XOR shift algorithm
  number ^= number << 13;
  number ^= number >> 17;
  number ^= number << 5;

  // update random number state
  random_state = number;

  // return random number
  return number;
}

// generate 64-bit pseudo legal numbers
uint64_t get_random_uint64_number(void) {
  // define 4 random numbers
  uint64_t n1, n2, n3, n4;

  // init random numbers slicing 16 bits from MS1B side
  n1 = (uint64_t)(get_random_U32_number()) & 0xFFFF;
  n2 = (uint64_t)(get_random_U32_number()) & 0xFFFF;
  n3 = (uint64_t)(get_random_U32_number()) & 0xFFFF;
  n4 = (uint64_t)(get_random_U32_number()) & 0xFFFF;

  // return random number
  return n1 | (n2 << 16) | (n3 << 32) | (n4 << 48);
}

// generate magic number candidate
uint64_t generate_magic_number(void) {
  return get_random_uint64_number() & get_random_uint64_number() &
         get_random_uint64_number();
}

// init random hash keys (zobrist keys)
static inline void init_random_keys(void) {
  // update pseudo random number state
  random_state = 1804289383;

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

  init_between_bitboards(between);

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
  threads = init_threads(thread_count);
  pos.enpassant = no_sq;
  limits.movestogo = 30;
  limits.time = -1;
  random_state = 1804289383;
  tt.hash_entry = NULL;
  tt.num_of_entries = 0;
  nnue_settings.nnue_file = calloc(21, 1);
  strcpy(nnue_settings.nnue_file, "hati.nnue");
  // init all
  init_all();

  // connect to GUI
  uci_loop(&pos, threads, argc, argv);

  // free hash table memory on exit
  free(tt.hash_entry);
  free(threads);
  free(nnue_settings.nnue_file);

  return 0;
}
