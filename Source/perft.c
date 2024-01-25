#include "bitboards.h"
#include "enums.h"
#include "movegen.h"
#include "structs.h"
#include "uci.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>

static inline void perft_driver(engine_t *engine, board_t *board, searchinfo_t *searchinfo, int depth) {
  // recursion escape condition
  if (depth == 0) {
    // increment nodes count (count reached positions)
    searchinfo->nodes++;
    return;
  }

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(board, move_list);

  // loop over generated moves
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // preserve board state
    copy_board(board->bitboards, board->occupancies,
               board->side, board->enpassant,
               board->castle, board->fifty, board->hash_key);

    // make move
    if (!make_move(engine, board, move_list->entry[move_count].move, all_moves))
      // skip to the next move
      continue;

    // call perft driver recursively
    perft_driver(engine, board, searchinfo, depth - 1);

    // take back
    restore_board(board->bitboards, board->occupancies,
                  board->side, board->enpassant,
                  board->castle, board->fifty, board->hash_key);
  }
}

// perft test
void perft_test(engine_t *engine, board_t* board, searchinfo_t *searchinfo, int depth) {
  printf("\n     Performance test\n\n");

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(board, move_list);

  // init start time
  long start = get_time_ms();

  // loop over generated moves
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // preserve board state
    copy_board(board->bitboards, board->occupancies,
               board->side, board->enpassant,
               board->castle, board->fifty, board->hash_key);

    // make move
    if (!make_move(engine, board, move_list->entry[move_count].move, all_moves))
      // skip to the next move
      continue;

    // cummulative nodes
    long cummulative_nodes = searchinfo->nodes;

    // call perft driver recursively
    perft_driver(engine, board, searchinfo, depth - 1);

    // old nodes
    long old_nodes = searchinfo->nodes - cummulative_nodes;
    (void)old_nodes;

    // take back
    restore_board(board->bitboards, board->occupancies,
                  board->side, board->enpassant,
                  board->castle, board->fifty, board->hash_key);

    // print move
    printf("     move: %s%s%c  nodes: %ld\n",
           square_to_coordinates[get_move_source(
               move_list->entry[move_count].move)],
           square_to_coordinates[get_move_target(
               move_list->entry[move_count].move)],
           get_move_promoted(move_list->entry[move_count].move)
               ? promoted_pieces[get_move_promoted(
                     move_list->entry[move_count].move)]
               : ' ',
           old_nodes);
  }

  // print results
  printf("\n    Depth: %d\n", depth);
#ifdef WIN64
  printf("    Nodes: %llu\n", engine->nodes);
  printf("     Time: %llu\n\n", get_time_ms() - start);
#else
  printf("    Nodes: %lu\n", searchinfo->nodes);
  printf("     Time: %lu\n\n", get_time_ms() - start);
#endif
}
