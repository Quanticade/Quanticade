#include "enums.h"
#include "macros.h"
#include "movegen.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>

static inline void perft_driver(engine_t *engine, int depth) {
  // recursion escape condition
  if (depth == 0) {
    // increment nodes count (count reached positions)
    engine->nodes++;
    return;
  }

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(engine, move_list);

  // loop over generated moves
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    // make move
    if (!make_move(engine, move_list->entry[move_count].move, all_moves))
      // skip to the next move
      continue;

    // call perft driver recursively
    perft_driver(engine, depth - 1);

    // take back
    restore_board(engine->board.bitboards, engine->board.occupancies,
                  engine->board.side, engine->board.enpassant,
                  engine->board.castle, engine->fifty, engine->board.hash_key);
  }
}

// perft test
void perft_test(engine_t *engine, int depth) {
  printf("\n     Performance test\n\n");

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(engine, move_list);

  // init start time
  long start = get_time_ms();

  // loop over generated moves
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    // make move
    if (!make_move(engine, move_list->entry[move_count].move, all_moves))
      // skip to the next move
      continue;

    // cummulative nodes
    long cummulative_nodes = engine->nodes;

    // call perft driver recursively
    perft_driver(engine, depth - 1);

    // old nodes
    long old_nodes = engine->nodes - cummulative_nodes;
	(void)old_nodes;

    // take back
    restore_board(engine->board.bitboards, engine->board.occupancies,
                  engine->board.side, engine->board.enpassant,
                  engine->board.castle, engine->fifty, engine->board.hash_key);

    // print move
	// Rewrite once we get printing functions organized
    /*printf("     move: %s%s%c  nodes: %ld\n",
           square_to_coordinates[get_move_source(
               move_list->entry[move_count].move)],
           square_to_coordinates[get_move_target(
               move_list->entry[move_count].move)],
           get_move_promoted(move_list->entry[move_count].move)
               ? promoted_pieces[get_move_promoted(
                     move_list->entry[move_count].move)]
               : ' ',
           old_nodes);*/
  }

  // print results
  printf("\n    Depth: %d\n", depth);
#ifdef WIN64
  printf("    Nodes: %llu\n", engine->nodes);
  printf("     Time: %llu\n\n", get_time_ms() - start);
#else
  printf("    Nodes: %lu\n", engine->nodes);
  printf("     Time: %lu\n\n", get_time_ms() - start);
#endif
}
