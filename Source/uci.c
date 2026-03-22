#include "uci.h"
#include "attacks.h"
#include "bitboards.h"
#include "datagen.h"
#include "enums.h"
#include "history.h"
#include "move.h"
#include "movegen.h"
#include "nnue.h"
#include "perft.h"
#include "pyrrhic/tbprobe.h"
#include "search.h"
#include "spsa.h"
#include "structs.h"
#include "threads.h"
#include "transposition.h"
#include "utils.h"
#include <ctype.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern nnue_settings_t nnue_settings;

static inline int istrncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    const int diff = tolower((unsigned char)a[i]) - tolower((unsigned char)b[i]);
    if (diff != 0 || a[i] == '\0') return diff;
  }
  return 0;
}

const int default_hash_size = 16;

int thread_count = 1;

int32_t move_overhead = 10;

uint8_t disable_norm = 0;
uint8_t soft_nodes = 0;
uint8_t minimal = 0;

double DEF_TIME_MULTIPLIER = 0.09087583539486617f;
double DEF_INC_MULTIPLIER = 0.8482586046941052f;
double MAX_TIME_MULTIPLIER = 0.7475832965589099f;
double SOFT_LIMIT_MULTIPLIER = 0.7781139811583045f;

char *bench_positions[] = {
    "r3k2r/2pb1ppp/2pp1q2/p7/1nP1B3/1P2P3/P2N1PPP/R2QK2R w KQkq a6 0 14",
    "4rrk1/2p1b1p1/p1p3q1/4p3/2P2n1p/1P1NR2P/PB3PP1/3R1QK1 b - - 2 24",
    "r3qbrk/6p1/2b2pPp/p3pP1Q/PpPpP2P/3P1B2/2PB3K/R5R1 w - - 16 42",
    "6k1/1R3p2/6p1/2Bp3p/3P2q1/P7/1P2rQ1K/5R2 b - - 4 44",
    "8/8/1p2k1p1/3p3p/1p1P1P1P/1P2PK2/8/8 w - - 3 54",
    "7r/2p3k1/1p1p1qp1/1P1Bp3/p1P2r1P/P7/4R3/Q4RK1 w - - 0 36",
    "r1bq1rk1/pp2b1pp/n1pp1n2/3P1p2/2P1p3/2N1P2N/PP2BPPP/R1BQ1RK1 b - - 2 10",
    "3r3k/2r4p/1p1b3q/p4P2/P2Pp3/1B2P3/3BQ1RP/6K1 w - - 3 87",
    "2r4r/1p4k1/1Pnp4/3Qb1pq/8/4BpPp/5P2/2RR1BK1 w - - 0 42",
    "4q1bk/6b1/7p/p1p4p/PNPpP2P/KN4P1/3Q4/4R3 b - - 0 37",
    "2q3r1/1r2pk2/pp3pp1/2pP3p/P1Pb1BbP/1P4Q1/R3NPP1/4R1K1 w - - 2 34",
    "1r2r2k/1b4q1/pp5p/2pPp1p1/P3Pn2/1P1B1Q1P/2R3P1/4BR1K b - - 1 37",
    "r3kbbr/pp1n1p1P/3ppnp1/q5N1/1P1pP3/P1N1B3/2P1QP2/R3KB1R b KQkq b3 0 17",
    "8/6pk/2b1Rp2/3r4/1R1B2PP/P5K1/8/2r5 b - - 16 42",
    "1r4k1/4ppb1/2n1b1qp/pB4p1/1n1BP1P1/7P/2PNQPK1/3RN3 w - - 8 29",
    "8/p2B4/PkP5/4p1pK/4Pb1p/5P2/8/8 w - - 29 68",
    "3r4/ppq1ppkp/4bnp1/2pN4/2P1P3/1P4P1/PQ3PBP/R4K2 b - - 2 20",
    "5rr1/4n2k/4q2P/P1P2n2/3B1p2/4pP2/2N1P3/1RR1K2Q w - - 1 49",
    "1r5k/2pq2p1/3p3p/p1pP4/4QP2/PP1R3P/6PK/8 w - - 1 51",
    "q5k1/5ppp/1r3bn1/1B6/P1N2P2/BQ2P1P1/5K1P/8 b - - 2 34",
    "r1b2k1r/5n2/p4q2/1ppn1Pp1/3pp1p1/NP2P3/P1PPBK2/1RQN2R1 w - - 0 22",
    "r1bqk2r/pppp1ppp/5n2/4b3/4P3/P1N5/1PP2PPP/R1BQKB1R w KQkq - 0 5",
    "r1bqr1k1/pp1p1ppp/2p5/8/3N1Q2/P2BB3/1PP2PPP/R3K2n b Q - 1 12",
    "r1bq2k1/p4r1p/1pp2pp1/3p4/1P1B3Q/P2B1N2/2P3PP/4R1K1 b - - 2 19",
    "r4qk1/6r1/1p4p1/2ppBbN1/1p5Q/P7/2P3PP/5RK1 w - - 2 25",
    "r7/6k1/1p6/2pp1p2/7Q/8/p1P2K1P/8 w - - 0 32",
    "r3k2r/ppp1pp1p/2nqb1pn/3p4/4P3/2PP4/PP1NBPPP/R2QK1NR w KQkq - 1 5",
    "3r1rk1/1pp1pn1p/p1n1q1p1/3p4/Q3P3/2P5/PP1NBPPP/4RRK1 w - - 0 12",
    "5rk1/1pp1pn1p/p3Brp1/8/1n6/5N2/PP3PPP/2R2RK1 w - - 2 20",
    "8/1p2pk1p/p1p1r1p1/3n4/8/5R2/PP3PPP/4R1K1 b - - 3 27",
    "8/4pk2/1p1r2p1/p1p4p/Pn5P/3R4/1P3PP1/4RK2 w - - 1 33",
    "8/5k2/1pnrp1p1/p1p4p/P6P/4R1PK/1P3P2/4R3 b - - 1 38",
    "8/8/1p1kp1p1/p1pr1n1p/P6P/1R4P1/1P3PK1/1R6 b - - 15 45",
    "8/8/1p1k2p1/p1prp2p/P2n3P/6P1/1P1R1PK1/4R3 b - - 5 49",
    "8/8/1p4p1/p1p2k1p/P2npP1P/4K1P1/1P6/3R4 w - - 6 54",
    "8/8/1p4p1/p1p2k1p/P2n1P1P/4K1P1/1P6/6R1 b - - 6 59",
    "8/5k2/1p4p1/p1pK3p/P2n1P1P/6P1/1P6/4R3 b - - 14 63",
    "8/1R6/1p1K1kp1/p6p/P1p2P1P/6P1/1Pn5/8 w - - 0 67",
    "1rb1rn1k/p3q1bp/2p3p1/2p1p3/2P1P2N/PP1RQNP1/1B3P2/4R1K1 b - - 4 23",
    "4rrk1/pp1n1pp1/q5p1/P1pP4/2n3P1/7P/1P3PB1/R1BQ1RK1 w - - 3 22",
    "r2qr1k1/pb1nbppp/1pn1p3/2ppP3/3P4/2PB1NN1/PP3PPP/R1BQR1K1 w - - 4 12",
    "2r2k2/8/4P1R1/1p6/8/P4K1N/7b/2B5 b - - 0 55",
    "6k1/5pp1/8/2bKP2P/2P5/p4PNb/B7/8 b - - 1 44",
    "2rqr1k1/1p3p1p/p2p2p1/P1nPb3/2B1P3/5P2/1PQ2NPP/R1R4K w - - 3 25",
    "r1b2rk1/p1q1ppbp/6p1/2Q5/8/4BP2/PPP3PP/2KR1B1R b - - 2 14",
    "6r1/5k2/p1b1r2p/1pB1p1p1/1Pp3PP/2P1R1K1/2P2P2/3R4 w - - 1 36",
    "rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3 0 2",
    "2rr2k1/1p4bp/p1q1p1p1/4Pp1n/2PB4/1PN3P1/P3Q2P/2RR2K1 w - f6 0 20",
    "3br1k1/p1pn3p/1p3n2/5pNq/2P1p3/1PN3PP/P2Q1PB1/4R1K1 w - - 0 23",
    "2r2b2/5p2/5k2/p1r1pP2/P2pB3/1P3P2/K1P3R1/7R w - - 23 93"};

#define start_position \
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 "

const char *square_to_coordinates[] = {
    "a8", "b8", "c8", "d8", "e8", "f8", "g8", "h8", "a7", "b7", "c7",
    "d7", "e7", "f7", "g7", "h7", "a6", "b6", "c6", "d6", "e6", "f6",
    "g6", "h6", "a5", "b5", "c5", "d5", "e5", "f5", "g5", "h5", "a4",
    "b4", "c4", "d4", "e4", "f4", "g4", "h4", "a3", "b3", "c3", "d3",
    "e3", "f3", "g3", "h3", "a2", "b2", "c2", "d2", "e2", "f2", "g2",
    "h2", "a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1",
};

int char_pieces[] = {
    ['P'] = P, ['N'] = N, ['B'] = B, ['R'] = R, ['Q'] = Q, ['K'] = K,
    ['p'] = p, ['n'] = n, ['b'] = b, ['r'] = r, ['q'] = q, ['k'] = k};

char piece_chars[] = {
    [P] = 'P', [N] = 'N', [B] = 'B', [R] = 'R', [Q] = 'Q', [K] = 'K',
    [p] = 'p', [n] = 'n', [b] = 'b', [r] = 'r', [q] = 'q', [k] = 'k'};

char promoted_pieces[] = {[Q] = 'q', [R] = 'r', [B] = 'b', [N] = 'n',
                          [q] = 'q', [r] = 'r', [b] = 'b', [n] = 'n'};

static inline int parse_move(position_t *pos, thread_t *thread,
                              char *move_string) {
  moves move_list[1];
  generate_noisy(pos, move_list, 0);
  generate_quiets(pos, move_list, 1);

  const int source_square = (move_string[0] - 'a') + (8 - (move_string[1] - '0')) * 8;
  thread->starttime = 0;
  const int target_square = (move_string[2] - 'a') + (8 - (move_string[3] - '0')) * 8;

  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    const int move = move_list->entry[move_count].move;

    if (source_square != get_move_source(move) ||
        target_square != get_move_target(move))
      continue;

    const int promoted_piece = get_move_promoted(pos->side, move);
    if (promoted_piece) {
      if (move_string[4] && promoted_pieces[promoted_piece] == move_string[4]) return move;
      continue;
    }

    return move;
  }

  return 0;
}

static inline void reset_board(position_t *pos, thread_t *thread) {
  memset(pos->bitboards, 0ULL, sizeof(pos->bitboards));
  memset(pos->occupancies, 0ULL, sizeof(pos->occupancies));
  pos->side = 0;
  pos->enpassant = no_sq;
  pos->castle = 0;
  pos->checkers = 0;
  pos->checker_count = 0;
  thread->repetition_index = 0;
  pos->fifty = 0;
  memset(thread->repetition_table, 0ULL, sizeof(thread->repetition_table));
}

void generate_fen(position_t *pos, char *fen) {
  char *ptr = fen;

  for (int rank = 0; rank < 8; rank++) {
    int empty = 0;

    for (int file = 0; file < 8; file++) {
      const int piece = pos->mailbox[rank * 8 + file];
      if (piece != NO_PIECE) {
        if (empty > 0) { *ptr++ = '0' + empty; empty = 0; }
        *ptr++ = piece_chars[piece];
      } else {
        empty++;
      }
    }

    if (empty > 0) *ptr++ = '0' + empty;
    if (rank < 7)  *ptr++ = '/';
  }

  *ptr++ = ' ';
  *ptr++ = (pos->side == white) ? 'w' : 'b';
  *ptr++ = ' ';

  if (pos->castle == 0) {
    *ptr++ = '-';
  } else {
    if (pos->castle & wk) *ptr++ = 'K';
    if (pos->castle & wq) *ptr++ = 'Q';
    if (pos->castle & bk) *ptr++ = 'k';
    if (pos->castle & bq) *ptr++ = 'q';
  }

  *ptr++ = ' ';
  if (pos->enpassant == no_sq) {
    *ptr++ = '-';
  } else {
    *ptr++ = 'a' + (pos->enpassant % 8);
    *ptr++ = '1' + (7 - pos->enpassant / 8);
  }

  *ptr++ = ' ';
  ptr += sprintf(ptr, "%d", pos->fifty);
  *ptr++ = ' ';
  sprintf(ptr, "%d", pos->fullmove);
}

static inline void parse_fen(position_t *pos, thread_t *thread, char *fen) {
  reset_board(pos, thread);

  for (int rank = 0; rank < 8; rank++) {
    for (int file = 0; file < 8; file++) {
      const int square = rank * 8 + file;

      if ((*fen >= 'a' && *fen <= 'z') || (*fen >= 'A' && *fen <= 'Z')) {
        const int piece = char_pieces[*(uint8_t *)fen];
        set_bit(pos->bitboards[piece], square);
        pos->mailbox[square] = piece;
        fen++;
      }

      if (*fen >= '0' && *fen <= '9') {
        const int offset = *fen - '0';
        const uint8_t bb_piece = pos->mailbox[square];
        if (!(bb_piece != NO_PIECE && get_bit(pos->bitboards[bb_piece], square)))
          file--;
        file += offset;
        fen++;
      }

      if (*fen == '/') fen++;
    }
  }

  fen++;
  pos->side = (*fen == 'w') ? white : black;
  fen += 2;

  while (*fen && *fen != ' ') {
    switch (*fen++) {
    case 'K': pos->castle |= wk; break;
    case 'Q': pos->castle |= wq; break;
    case 'k': pos->castle |= bk; break;
    case 'q': pos->castle |= bq; break;
    }
  }

  fen++;
  if (*fen != '-') {
    pos->enpassant = (8 - (fen[1] - '0')) * 8 + (fen[0] - 'a');
    fen += 2;
  } else {
    pos->enpassant = no_sq;
    fen++;
  }

  fen++;
  pos->fifty = atoi(fen);
  while (*fen && *fen != ' ') fen++;
  pos->fullmove = (*fen == ' ') ? atoi(fen + 1) : 1;

  for (int piece = P; piece <= K; piece++) pos->occupancies[white] |= pos->bitboards[piece];
  for (int piece = p; piece <= k; piece++) pos->occupancies[black] |= pos->bitboards[piece];
  pos->occupancies[both] = pos->occupancies[white] | pos->occupancies[black];

  pos->hash_keys.hash_key = generate_hash_key(pos);
  pos->hash_keys.pawn_key = generate_pawn_key(pos);
  pos->hash_keys.non_pawn_key[white] = generate_white_non_pawn_key(pos);
  pos->hash_keys.non_pawn_key[black] = generate_black_non_pawn_key(pos);

  pos->checkers = attackers_to(pos,
                               (pos->side == white) ? get_lsb(pos->bitboards[K])
                                                    : get_lsb(pos->bitboards[k]),
                               pos->occupancies[both]) &
                  pos->occupancies[pos->side ^ 1];
  pos->checker_count = popcount(pos->checkers);
  update_slider_pins(pos, white);
  update_slider_pins(pos, black);
}

void parse_position(position_t *pos, thread_t *thread, char *command) {
  command += 9;

  for (int i = 0; i < 64; ++i) pos->mailbox[i] = NO_PIECE;

  if (strncmp(command, "startpos", 8) == 0) {
    parse_fen(pos, thread, start_position);
  } else {
    char *fen = strstr(command, "fen");
    parse_fen(pos, thread, fen ? fen + 4 : start_position);
  }

  char *moves = strstr(command, "moves");
  if (!moves) return;

  moves += 6;
  while (*moves) {
    const int move = parse_move(pos, thread, moves);
    if (!move) break;

    if (thread->repetition_index + 1 < (int)(sizeof(thread->repetition_table) / sizeof(thread->repetition_table[0]))) {
      thread->repetition_index++;
      thread->repetition_table[thread->repetition_index] = pos->hash_keys.hash_key;
    }
    make_move(pos, move);

    while (*moves && *moves != ' ') moves++;
    moves++;
  }
}

void time_control(position_t *pos, thread_t *threads, char *line) {
  threads->stopped = 0;
  threads->quit = 0;
  threads->starttime = 0;
  memset(&limits, 0, sizeof(limits_t));

  threads[0].starttime = get_time_ms();

  char *argument = NULL;

  if (pos->side == white) {
    if ((argument = strstr(line, "winc")))  limits.inc  = atoi(argument + 5);
    if ((argument = strstr(line, "wtime"))) { limits.time = atoi(argument + 6); limits.timeset = 1; }
  } else {
    if ((argument = strstr(line, "binc")))  limits.inc  = atoi(argument + 5);
    if ((argument = strstr(line, "btime"))) { limits.time = atoi(argument + 6); limits.timeset = 1; }
  }

  if ((argument = strstr(line, "movestogo"))) limits.movestogo = atoi(argument + 10);

  if ((argument = strstr(line, "movetime"))) {
    limits.time = atoi(argument + 9);
    limits.movestogo = 1;
    limits.timeset = 1;
  }

  if ((argument = strstr(line, "nodes"))) {
    limits.node_limit_soft = atoi(argument + 6);
    limits.node_limit_hard = soft_nodes ? 10000000 : atoi(argument + 6);
    limits.depth = MAX_PLY;
    limits.nodes_set = 1;
  }

  if ((argument = strstr(line, "depth"))) {
    limits.depth = atoi(argument + 6);
  } else {
    limits.depth = limits.depth == 0 ? MAX_PLY : limits.depth;

    if (limits.timeset) {
      limits.time -= MIN(limits.time / 2, move_overhead);

      const int64_t base_time = (limits.movestogo > 0)
          ? (int64_t)((double)limits.time / limits.movestogo + limits.inc)
          : (int64_t)(limits.time * DEF_TIME_MULTIPLIER + limits.inc * DEF_INC_MULTIPLIER);

      limits.max_time   = MAX(1, limits.time * MAX_TIME_MULTIPLIER);
      limits.hard_limit = threads->starttime + limits.max_time;
      limits.base_soft  = MIN(base_time * SOFT_LIMIT_MULTIPLIER, limits.max_time);
      limits.soft_limit = threads->starttime + limits.base_soft;
    }
  }
}

static inline void *parse_go(void *searchthread_info) {
  searchthreadinfo_t *sti = (searchthreadinfo_t *)searchthread_info;
  char *perft_arg = strstr(sti->line, "perft");

  if (perft_arg) {
    limits.depth = atoi(perft_arg + 6);
    perft_test(sti->pos, sti->threads, limits.depth);
    return NULL;
  }

  time_control(sti->pos, sti->threads, sti->line);
  search_position(sti->pos, sti->threads);
  return NULL;
}

void print_move(int move) {
  if (is_move_promotion(move))
    printf("%s%s%c", square_to_coordinates[get_move_source(move)],
           square_to_coordinates[get_move_target(move)],
           promoted_pieces[get_move_promoted(white, move)]);
  else
    printf("%s%s", square_to_coordinates[get_move_source(move)],
           square_to_coordinates[get_move_target(move)]);
}

typedef struct {
  position_t         *pos;
  thread_t          **threads;
  int                *thread_count;
  int                 max_hash;
  pthread_t          *search_thread;
  uint8_t            *started;
  searchthreadinfo_t *sti;
  char               *input;
} uci_ctx_t;

static void stop_search(uci_ctx_t *ctx) {
  stop_threads(*ctx->threads, *ctx->thread_count);
  if (*ctx->started) {
    pthread_join(*ctx->search_thread, NULL);
    *ctx->started = 0;
  }
}

static void handle_isready(uci_ctx_t *ctx, char *args) {
  (void)ctx; (void)args;
  printf("readyok\n");
}

static void handle_position(uci_ctx_t *ctx, char *args) {
  (void)args;
  parse_position(ctx->pos, *ctx->threads, ctx->input);
  init_accumulator(ctx->pos, &(*ctx->threads)->accumulator[ctx->pos->ply]);
  init_finny_tables(*ctx->threads, ctx->pos);
}

static void handle_ucinewgame(uci_ctx_t *ctx, char *args) {
  (void)args;
  clear_hash_table();
  for (int i = 0; i < *ctx->thread_count; ++i) {
    thread_t *t = &(*ctx->threads)[i];
    memset(t->quiet_history,                 0, sizeof(t->quiet_history));
    memset(t->capture_history,               0, sizeof(t->capture_history));
    memset(t->continuation_history,          0, sizeof(t->continuation_history));
    memset(t->correction_history,            0, sizeof(t->correction_history));
    memset(t->pawn_history,                  0, sizeof(t->pawn_history));
    memset(t->w_non_pawn_correction_history, 0, sizeof(t->w_non_pawn_correction_history));
    memset(t->b_non_pawn_correction_history, 0, sizeof(t->b_non_pawn_correction_history));
  }
}

static void handle_go(uci_ctx_t *ctx, char *args) {
  (void)args;
  if (*ctx->started) pthread_join(*ctx->search_thread, NULL);
  printf("info string NNUE evaluation using %s\n", nnue_settings.nnue_file);
  strncpy(ctx->sti->line, ctx->input, sizeof(ctx->sti->line) - 1);
  ctx->sti->line[sizeof(ctx->sti->line) - 1] = '\0';
  pthread_create(ctx->search_thread, NULL, &parse_go, ctx->sti);
  *ctx->started = 1;
}

static void handle_stop(uci_ctx_t *ctx, char *args) {
  (void)args;
  stop_search(ctx);
}

static void handle_quit(uci_ctx_t *ctx, char *args) {
  (void)args;
  stop_search(ctx);
}

static void handle_uci(uci_ctx_t *ctx, char *args) {
  (void)args;
  printf("id name Quanticade %s\n", version);
  printf("id author DarkNeutrino\n\n");
  printf("option name Hash type spin default %d min 4 max %d\n", default_hash_size, ctx->max_hash);
  printf("option name Threads type spin default %d min %d max %d\n", 1, 1, 1024);
  printf("option name MoveOverhead type spin default 10 min 0 max 5000\n");
  printf("option name EvalFile type string default %s\n", nnue_settings.nnue_file);
  printf("option name Clear Hash type button\n");
  printf("option name SoftNodes type check default false\n");
  printf("option name DisableNormalization type check default false\n");
  printf("option name Minimal type check default false\n");
  print_spsa_table_uci();
  printf("uciok\n");
}

static void handle_spsa(uci_ctx_t *ctx, char *args) {
  (void)ctx; (void)args;
  print_spsa_table();
}

typedef struct {
  const char *prefix;
  void (*handler)(uci_ctx_t *, char *);
  uint8_t quit_after;
} uci_command_t;

static const uci_command_t uci_commands[] = {
  { "isready",    handle_isready,    0 },
  { "position",   handle_position,   0 },
  { "ucinewgame", handle_ucinewgame, 0 },
  { "go",         handle_go,         0 },
  { "stop",       handle_stop,       0 },
  { "quit",       handle_quit,       1 },
  { "uci",        handle_uci,        0 },
  { "spsa",       handle_spsa,       0 },
};

static void setoption_hash(uci_ctx_t *ctx, char *value) {
  int mb = atoi(value);
  mb = MAX(4, MIN(mb, ctx->max_hash));
  init_hash_table(mb);
}

static void setoption_threads(uci_ctx_t *ctx, char *value) {
  *ctx->thread_count = MAX(1, atoi(value));
#ifndef _WIN32
  free(*ctx->threads);
#else
  _aligned_free(*ctx->threads);
#endif
  *ctx->threads = init_threads(*ctx->thread_count);
  ctx->sti->threads = *ctx->threads;
}

static void setoption_eval_file(uci_ctx_t *ctx, char *value) {
  (void)ctx;
  char *new_path = strdup(value);
  if (!new_path) return;
  free(nnue_settings.nnue_file);
  nnue_settings.nnue_file = new_path;
  nnue_init(nnue_settings.nnue_file);
}

static void setoption_clear_hash(uci_ctx_t *ctx, char *value) {
  (void)ctx; (void)value;
  clear_hash_table();
}

static void setoption_syzygy_path(uci_ctx_t *ctx, char *value) {
  (void)ctx;
  printf("info string set SyzygyPath to %s\n", value);
}

typedef struct {
  const char *name;
  void (*handler)(uci_ctx_t *, char *);
} setoption_entry_t;

static void setoption_bool(uci_ctx_t *ctx, char *value, uint8_t *target) {
  (void)ctx;
  *target = (istrncmp(value, "true", 5) == 0) ? 1 : 0;
}

#define SETOPTION_BOOL(name, global) \
  static void setoption_##name(uci_ctx_t *ctx, char *value) { \
    setoption_bool(ctx, value, &global); \
  }

SETOPTION_BOOL(soft_nodes,   soft_nodes)
SETOPTION_BOOL(disable_norm, disable_norm)
SETOPTION_BOOL(minimal,      minimal)

static void setoption_move_overhead(uci_ctx_t *ctx, char *value) {
  (void)ctx;
  move_overhead = atoi(value);
}

static const setoption_entry_t setoption_table[] = {
  { "Hash",                 setoption_hash          },
  { "Threads",              setoption_threads       },
  { "MoveOverhead",         setoption_move_overhead },
  { "EvalFile",             setoption_eval_file     },
  { "Clear Hash",           setoption_clear_hash    },
  { "SyzygyPath",           setoption_syzygy_path   },
  { "SoftNodes",            setoption_soft_nodes    },
  { "DisableNormalization", setoption_disable_norm  },
  { "Minimal",              setoption_minimal       },
};

static void handle_setoption(uci_ctx_t *ctx, char *input) {
  char *name_start = NULL;
  for (char *p = input; *p; p++) {
    if (istrncmp(p, "name ", 5) == 0) { name_start = p + 5; break; }
  }
  if (!name_start) return;

  char *value_start = strstr(name_start, " value ");
  char name[256]  = {0};
  char value[256] = {0};

  if (value_start) {
    const size_t name_len = MIN((size_t)(value_start - name_start), sizeof(name) - 1);
    strncpy(name, name_start, name_len);
    strncpy(value, value_start + 7, sizeof(value) - 1);
    value[strcspn(value, "\r\n")] = '\0';
  } else {
    strncpy(name, name_start, sizeof(name) - 1);
    name[strcspn(name, "\r\n")] = '\0';
  }

  static const int n = sizeof(setoption_table) / sizeof(setoption_table[0]);
  for (int i = 0; i < n; ++i) {
    if (istrncmp(name, setoption_table[i].name, sizeof(name)) == 0) {
      setoption_table[i].handler(ctx, value);
      return;
    }
  }

  handle_spsa_change(input);
}

void uci_loop(position_t *pos, int argc, char *argv[]) {
  const int max_hash = 524288;

  thread_t *threads = init_threads(thread_count);

  pthread_t search_thread;
  uint8_t started = 0;
  searchthreadinfo_t sti = { .threads = threads, .pos = pos };

#ifndef WIN64
  setbuf(stdin, NULL);
#endif
  setbuf(stdout, NULL);

  char input[10000];

  printf("Quanticade %s by DarkNeutrino\n", version);

  parse_position(pos, threads, "position startpos");
  init_accumulator(pos, &threads->accumulator[pos->ply]);
  init_finny_tables(threads, pos);

  uci_ctx_t ctx = {
    .pos           = pos,
    .threads       = &threads,
    .thread_count  = &thread_count,
    .max_hash      = max_hash,
    .search_thread = &search_thread,
    .started       = &started,
    .sti           = &sti,
    .input         = input,
  };

  if (argc >= 2) {
    if (strncmp("bench", argv[1], 5) == 0) {
      minimal = 1;
      uint64_t total_nodes = 0;
      const uint64_t start_time = get_time_ms();
      for (int i = 0; i < 50; ++i) {
        memset(input, 0, sizeof(input));
        strcpy(input, "position fen ");
        strcat(input, bench_positions[i]);
        memset(threads, 0, sizeof(thread_t));
        printf("\nPosition %d/%d (%s)\n", i, 49, bench_positions[i]);
        parse_position(pos, threads, input);
        init_accumulator(pos, &threads->accumulator[pos->ply]);
        init_finny_tables(threads, pos);
        time_control(pos, threads, "go depth 13");
        search_position(pos, threads);
        total_nodes += threads->nodes;
      }
      printf("\n%" PRIu64 " nodes %" PRIu64 " nps\n",
             total_nodes, total_nodes / (get_time_ms() - start_time + 1) * 1000);
      return;
    } else if (strncmp("genfens", argv[1], 7) == 0) {
      minimal = 1;
      int n_of_fens = 0;
      uint64_t seed = 0ULL;
      char book[256];
      int n_of_char_read = 0;
      sscanf(argv[1], "genfens %d seed %99" SCNu64 " book %s %n",
             &n_of_fens, &seed, book, &n_of_char_read);
      genfens(pos, threads, seed, n_of_fens, book);
      return;
    }
  }

  static const int n_commands = sizeof(uci_commands) / sizeof(uci_commands[0]);

  while (1) {
    memset(input, 0, sizeof(input));
    fflush(stdout);

    if (!fgets(input, sizeof(input), stdin)) continue;

    // If no newline was read, the line was too long — drain the rest and discard
    if (!strchr(input, '\n')) {
      int ch;
      while ((ch = getchar()) != '\n' && ch != EOF);
      continue;
    }

    if (input[0] == '\n') continue;

    if (strncmp(input, "setoption", 9) == 0) {
      handle_setoption(&ctx, input);
      continue;
    }

    for (int i = 0; i < n_commands; ++i) {
      const size_t len = strlen(uci_commands[i].prefix);
      if (strncmp(input, uci_commands[i].prefix, len) == 0) {
        uci_commands[i].handler(&ctx, input + len);
        if (uci_commands[i].quit_after) goto done;
        break;
      }
    }
  }

done:
#ifndef _WIN32
  free(threads);
#else
  _aligned_free(threads);
#endif
}