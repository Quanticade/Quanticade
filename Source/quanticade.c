// system headers
#include "evaluate.h"
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
#include "nnue/nnue.h"
#include "pvtable.h"
#include "structs.h"
#include "uci.h"

engine_t engine;

// generate 32-bit pseudo legal numbers
uint32_t get_random_U32_number(engine_t *engine) {
  // get current state
  uint32_t number = engine->random_state;

  // XOR shift algorithm
  number ^= number << 13;
  number ^= number >> 17;
  number ^= number << 5;

  // update random number state
  engine->random_state = number;

  // return random number
  return number;
}

// generate 64-bit pseudo legal numbers
uint64_t get_random_uint64_number(engine_t *engine) {
  // define 4 random numbers
  uint64_t n1, n2, n3, n4;

  // init random numbers slicing 16 bits from MS1B side
  n1 = (uint64_t)(get_random_U32_number(engine)) & 0xFFFF;
  n2 = (uint64_t)(get_random_U32_number(engine)) & 0xFFFF;
  n3 = (uint64_t)(get_random_U32_number(engine)) & 0xFFFF;
  n4 = (uint64_t)(get_random_U32_number(engine)) & 0xFFFF;

  // return random number
  return n1 | (n2 << 16) | (n3 << 32) | (n4 << 48);
}

// generate magic number candidate
uint64_t generate_magic_number(engine_t *engine) {
  return get_random_uint64_number(engine) & get_random_uint64_number(engine) &
         get_random_uint64_number(engine);
}

// init random hash keys (zobrist keys)
static inline void init_random_keys(engine_t *engine) {
  // update pseudo random number state
  engine->random_state = 1804289383;

  // loop over piece codes
  for (int piece = P; piece <= k; piece++) {
    // loop over board squares
    for (int square = 0; square < 64; square++)
      // init random piece keys
      engine->keys.piece_keys[piece][square] = get_random_uint64_number(engine);
  }

  // loop over board squares
  for (int square = 0; square < 64; square++)
    // init random enpassant keys
    engine->keys.enpassant_keys[square] = get_random_uint64_number(engine);

  // loop over castling keys
  for (int index = 0; index < 16; index++)
    // init castling keys
    engine->keys.castle_keys[index] = get_random_uint64_number(engine);

  // init random side key
  engine->keys.side_key = get_random_uint64_number(engine);
}

// init all variables
void init_all(engine_t *engine, tt_t *hash_table) {
  // init leaper pieces attacks
  init_leapers_attacks();

  // init slider pieces attacks
  init_sliders_attacks();

  // init random keys for hashing purposes
  init_random_keys(engine);

  // init evaluation masks
  init_evaluation_masks();

  // init hash table with default 128 MB
  init_hash_table(engine, hash_table, 128);

  if (engine->nnue) {
    nnue_init("nn-eba324f53044.nnue");
  }
}

/**********************************\
 ==================================

             Main driver

 ==================================
\**********************************/

int main(void) {
  memset(&engine, 0, sizeof(engine));
  engine.board.enpassant = no_sq;
  engine.movestogo = 30;
  engine.time = -1;
  engine.nnue = 1;
  engine.random_state = 1804289383;
  tt_t hash_table = {NULL, 0, 0};
  engine.nnue_file = calloc(21, 1);
  strcpy(engine.nnue_file, "nn-eba324f53044.nnue");
  // init all
  init_all(&engine, &hash_table);

  // connect to GUI
  uci_loop(&engine, &hash_table);

  // free hash table memory on exit
  free(hash_table.hash_entry);

  return 0;
}
