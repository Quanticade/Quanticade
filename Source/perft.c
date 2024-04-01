#include "bitboards.h"
#include "enums.h"
#include "movegen.h"
#include "structs.h"
#include "uci.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>

static inline void perft_driver(position_t *pos, thread_t *thread, int depth) {
  // recursion escape condition
  if (depth == 0) {
    // increment nodes count (count reached positions)
    thread->nodes++;
    return;
  }

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(pos, move_list);

  // loop over generated moves
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // preserve board state
    copy_board(pos->bitboards, pos->occupancies,
               pos->side, pos->enpassant,
               pos->castle, pos->fifty, pos->hash_key);

    // make move
    if (!make_move(pos, move_list->entry[move_count].move, all_moves))
      // skip to the next move
      continue;

    // call perft driver recursively
    perft_driver(pos, thread, depth - 1);

    // take back
    restore_board(pos->bitboards, pos->occupancies,
                  pos->side, pos->enpassant,
                  pos->castle, pos->fifty, pos->hash_key);
  }
}

// perft test
void perft_test(position_t* pos, thread_t *searchinfo, int depth) {
  printf("\n     Performance test\n\n");

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(pos, move_list);

  // init start time
  long start = get_time_ms();

  // loop over generated moves
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // preserve board state
    copy_board(pos->bitboards, pos->occupancies,
               pos->side, pos->enpassant,
               pos->castle, pos->fifty, pos->hash_key);

    // make move
    if (!make_move(pos, move_list->entry[move_count].move, all_moves))
      // skip to the next move
      continue;

    // cummulative nodes
    long cummulative_nodes = searchinfo->nodes;

    // call perft driver recursively
    perft_driver(pos, searchinfo, depth - 1);

    // old nodes
    long old_nodes = searchinfo->nodes - cummulative_nodes;
    (void)old_nodes;

    // take back
    restore_board(pos->bitboards, pos->occupancies,
                  pos->side, pos->enpassant,
                  pos->castle, pos->fifty, pos->hash_key);

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
  printf("      NPS: %llu\n\n", searchinfo->nodes / ((get_time_ms() - start) / 100));
#else
  printf("    Nodes: %lu\n", searchinfo->nodes);
  printf("     Time: %lu\n\n", get_time_ms() - start);
  printf("      NPS: %lu\n\n", searchinfo->nodes / ((get_time_ms() - start) / 100));
#endif
}
