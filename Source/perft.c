#include "bitboards.h"
#include "enums.h"
#include "move.h"
#include "movegen.h"
#include "structs.h"
#include "uci.h"
#include "utils.h"
#include <inttypes.h>
#include <math.h>
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
    position_t pos_copy = *pos;

    if (!is_legal(pos, move_list->entry[move_count].move)) {
      continue;
    }
    // make move
    if (!make_move(&pos_copy, move_list->entry[move_count].move)) {
      printf("is_legal allowed an illegal move\n");
      char fen[200];
      generate_fen(pos, fen);
      print_move(move_list->entry[move_count].move);
      printf(" is legal in position %s\n", fen);
      // skip to the next move
      continue;
    }

    // call perft driver recursively
    perft_driver(&pos_copy, thread, depth - 1);
  }
}

// perft test
void perft_test(position_t *pos, thread_t *searchinfo, int depth) {
  printf("\n     Performance test\n\n");

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(pos, move_list);

  // init start time
  long start = get_time_ms();

  // loop over generated moves
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    position_t pos_copy = *pos;

    if (!is_legal(pos, move_list->entry[move_count].move)) {
      continue;
    }

    // make move
    if (!make_move(&pos_copy, move_list->entry[move_count].move))
      // skip to the next move
      continue;

    // cummulative nodes
    long cummulative_nodes = searchinfo->nodes;

    // call perft driver recursively
    perft_driver(&pos_copy, searchinfo, depth - 1);

    // old nodes
    long old_nodes = searchinfo->nodes - cummulative_nodes;
    (void)old_nodes;

    // print move
    printf("     move: %s%s%c  nodes: %ld\n",
           square_to_coordinates[get_move_source(
               move_list->entry[move_count].move)],
           square_to_coordinates[get_move_target(
               move_list->entry[move_count].move)],
           is_move_promotion(move_list->entry[move_count].move)
               ? promoted_pieces[get_move_promoted(
                     pos->side, move_list->entry[move_count].move)]
               : ' ',
           old_nodes);
  }

  // print results
  printf("\n    Depth: %d\n", depth);
  uint64_t nps = (searchinfo->nodes / fmax(get_time_ms() - start, 1)) * 1000;
  printf("    Nodes: %" PRIu64 "\n", searchinfo->nodes);
  printf("     Time: %" PRIu64 "\n\n", get_time_ms() - start);
  printf("      NPS: %" PRIu64 "\n\n", nps);
}
