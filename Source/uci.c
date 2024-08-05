/**********************************\
 ==================================

                UCI
          forked from VICE
         by Richard Allbert

 ==================================
\**********************************/

#include "uci.h"
#include "bitboards.h"
#include "enums.h"
#include "movegen.h"
#include "nnue.h"
#include "perft.h"
#include "pvtable.h"
#include "pyrrhic/tbprobe.h"
#include "search.h"
#include "structs.h"
#include "threads.h"
#include "utils.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern nnue_settings_t nnue_settings;

extern int LMP_BASE;
extern int LMP_MULTIPLIER;
extern int RAZOR_DEPTH;
extern int RAZOR_MARGIN;
extern int RFP_DEPTH;
extern int RFP_MARGIN;
extern int NMP_BASE_REDUCTION;
extern int NMP_DIVISER;
extern int NMP_DEPTH;
extern int IID_DEPTH;
extern int IID_REDUCTION;

const int default_hash_size = 16;

int thread_count = 1;

typedef struct see_pos {
  char fen[66];
  char move[6];
  int score;
  char explanation[34];
} see_positions;

see_positions see_pos[] = {
    {"2r1r1k1/pp1bppbp/3p1np1/q3P3/2P2P2/1P2B3/P1N1B1PP/2RQ1RK1 b - -", "d6e5",
     100, "P"},
    {"6k1/1pp4p/p1pb4/6q1/3P1pRr/2P4P/PP1Br1P1/5RKN w - -", "f1f4", -100,
     "P - R + B"},
    {"5rk1/1pp2q1p/p1pb4/8/3P1NP1/2P5/1P1BQ1P1/5RK1 b - -", "d6f4", 0,
     "-N + B"},
    {"4R3/2r3p1/5bk1/1p1r3p/p2PR1P1/P1BK1P2/1P6/8 b - -", "h5g4", 0, ""},
    {"4R3/2r3p1/5bk1/1p1r1p1p/p2PR1P1/P1BK1P2/1P6/8 b - -", "h5g4", 0, ""},
    {"4r1k1/5pp1/nbp4p/1p2p2q/1P2P1b1/1BP2N1P/1B2QPPK/3R4 b - -", "g4f3", 0, ""},
    {"7r/5qpk/p1Qp1b1p/3r3n/BB3p2/5p2/P1P2P2/4RK1R w - -", "e1e8", 0, ""},
    {"6rr/6pk/p1Qp1b1p/2n5/1B3p2/5p2/P1P2P2/4RK1R w - -", "e1e8", -500, "-R"},
    {"7r/5qpk/2Qp1b1p/1N1r3n/BB3p2/5p2/P1P2P2/4RK1R w - -", "e1e8", -500,
     "-R"},
    {"6RR/4bP2/8/8/5r2/3K4/5p2/4k3 w - -", "f7f8q", 200, "B - P"},
    {"6RR/4bP2/8/8/5r2/3K4/5p2/4k3 w - -", "f7f8n", 200, "N - P"},
    {"7R/5P2/8/8/6r1/3K4/5p2/4k3 w - -", "f7f8q", 800, "Q - P"},
    {"7R/5P2/8/8/6r1/3K4/5p2/4k3 w - -", "f7f8b", 200, "B - P"},
    {"7R/4bP2/8/8/1q6/3K4/5p2/4k3 w - -", "f7f8r", -100, "-P"},
    {"8/4kp2/2npp3/1Nn5/1p2PQP1/7q/1PP1B3/4KR1r b - -", "h1f1", 0, ""},
    {"8/4kp2/2npp3/1Nn5/1p2P1P1/7q/1PP1B3/4KR1r b - -", "h1f1", 0, ""},
    {"2r2r1k/6bp/p7/2q2p1Q/3PpP2/1B6/P5PP/2RR3K b - -", "c5c1", 100,
     "R - Q + R"},
    {"r2qk1nr/pp2ppbp/2b3p1/2p1p3/8/2N2N2/PPPP1PPP/R1BQR1K1 w kq -", "f3e5",
     100, "P"},
    {"6r1/4kq2/b2p1p2/p1pPb3/p1P2B1Q/2P4P/2B1R1P1/6K1 w - -", "f4e5", 0, ""},
    {"3q2nk/pb1r1p2/np6/3P2Pp/2p1P3/2R4B/PQ3P1P/3R2K1 w - h6", "g5h6", 0, ""},
    {"3q2nk/pb1r1p2/np6/3P2Pp/2p1P3/2R1B2B/PQ3P1P/3R2K1 w - h6", "g5h6", 100,
     "P"},
    {"2r4r/1P4pk/p2p1b1p/7n/BB3p2/2R2p2/P1P2P2/4RK2 w - -", "c3c8", 500, "R"},
    {"2r5/1P4pk/p2p1b1p/5b1n/BB3p2/2R2p2/P1P2P2/4RK2 w - -", "c3c8", 300,
     "B"},
    {"2r4k/2r4p/p7/2b2p1b/4pP2/1BR5/P1R3PP/2Q4K w - -", "c3c5", 300, "B"},
    {"8/pp6/2pkp3/4bp2/2R3b1/2P5/PP4B1/1K6 w - -", "g2c6", -200, "P - B"},
    {"4q3/1p1pr1k1/1B2rp2/6p1/p3PP2/P3R1P1/1P2R1K1/4Q3 b - -", "e6e4", -400,
     "P - R"},
    {"4q3/1p1pr1kb/1B2rp2/6p1/p3PP2/P3R1P1/1P2R1K1/4Q3 b - -", "h7e4", 100,
     "P"},
    {"3r3k/3r4/2n1n3/8/3p4/2PR4/1B1Q4/3R3K w - -", "d3d4", -100,
     "P - R + N - P + N - B + R - Q + R"},
    {"1k1r4/1ppn3p/p4b2/4n3/8/P2N2P1/1PP1R1BP/2K1Q3 w - -", "d3e5", 100,
     "N - N + B - R + N"},
    {"1k1r3q/1ppn3p/p4b2/4p3/8/P2N2P1/1PP1R1BP/2K1Q3 w - -", "d3e5", -200,
     "P - N"},
    {"rnb2b1r/ppp2kpp/5n2/4P3/q2P3B/5R2/PPP2PPP/RN1QKB2 w Q -", "h4f6", 100,
     "N - B + P"},
    {"r2q1rk1/2p1bppp/p2p1n2/1p2P3/4P1b1/1nP1BN2/PP3PPP/RN1QR1K1 b - -", "g4f3",
     0, "N - B"},
    {"r1bqkb1r/2pp1ppp/p1n5/1p2p3/3Pn3/1B3N2/PPP2PPP/RNBQ1RK1 b kq -", "c6d4",
     0, "P - N + N - P"},
    {"r1bq1r2/pp1ppkbp/4N1p1/n3P1B1/8/2N5/PPP2PPP/R2QK2R w KQ -", "e6g7", 0,
     "B - N"},
    {"r1bq1r2/pp1ppkbp/4N1pB/n3P3/8/2N5/PPP2PPP/R2QK2R w KQ -", "e6g7", 300,
     "B"},
    {"rnq1k2r/1b3ppp/p2bpn2/1p1p4/3N4/1BN1P3/PPP2PPP/R1BQR1K1 b kq -", "d6h2",
     -200, "P - B"},
    {"rn2k2r/1bq2ppp/p2bpn2/1p1p4/3N4/1BN1P3/PPP2PPP/R1BQR1K1 b kq -", "d6h2",
     100, "P"},
    {"r2qkbn1/ppp1pp1p/3p1rp1/3Pn3/4P1b1/2N2N2/PPP2PPP/R1BQKB1R b KQq -",
     "g4f3", 100, "N - B + P"},
    {"rnbq1rk1/pppp1ppp/4pn2/8/1bPP4/P1N5/1PQ1PPPP/R1B1KBNR b KQ -", "b4c3",
     0, "N - B"},
    {"r4rk1/3nppbp/bq1p1np1/2pP4/8/2N2NPP/PP2PPB1/R1BQR1K1 b - -", "b6b2",
     -800, "P - Q"},
    {"r4rk1/1q1nppbp/b2p1np1/2pP4/8/2N2NPP/PP2PPB1/R1BQR1K1 b - -", "f6d5",
     -200, "P - N"},
    {"1r3r2/5p2/4p2p/2k1n1P1/2PN1nP1/1P3P2/8/2KR1B1R b - -", "b8b3", -400,
     "P - R"},
    {"1r3r2/5p2/4p2p/4n1P1/kPPN1nP1/5P2/8/2KR1B1R b - -", "b8b4", 100, "P"},
    {"2r2rk1/5pp1/pp5p/q2p4/P3n3/1Q3NP1/1P2PP1P/2RR2K1 b - -", "c8c1", 0,
     "R - R"},
    {"5rk1/5pp1/2r4p/5b2/2R5/6Q1/R1P1qPP1/5NK1 b - -", "f5c2", -100,
     "P - B + R - Q + R"},
    {"1r3r1k/p4pp1/2p1p2p/qpQP3P/2P5/3R4/PP3PP1/1K1R4 b - -", "a5a2", -800,
     "P - Q"},
    {"1r5k/p4pp1/2p1p2p/qpQP3P/2P2P2/1P1R4/P4rP1/1K1R4 b - -", "a5a2", 100,
     "P"},
    {"r2q1rk1/1b2bppp/p2p1n2/1ppNp3/3nP3/P2P1N1P/BPP2PP1/R1BQR1K1 w - -",
     "d5e7", 0, "B - N"},
    {"rnbqrbn1/pp3ppp/3p4/2p2k2/4p3/3B1K2/PPP2PPP/RNB1Q1NR w - -", "d3e4",
     100, "P"},
    {"rnb1k2r/p3p1pp/1p3p1b/7n/1N2N3/3P1PB1/PPP1P1PP/R2QKB1R w KQkq -", "e4d6",
     -200, "-N + P"},
    {"r1b1k2r/p4npp/1pp2p1b/7n/1N2N3/3P1PB1/PPP1P1PP/R2QKB1R w KQkq -", "e4d6",
     0, "-N + N"},
    {"2r1k2r/pb4pp/5p1b/2KB3n/4N3/2NP1PB1/PPP1P1PP/R2Q3R w k -", "d5c6", -300,
     "-B"},
    {"2r1k2r/pb4pp/5p1b/2KB3n/1N2N3/3P1PB1/PPP1P1PP/R2Q3R w k -", "d5c6", 0,
     "-B + B"},
    {"2r1k3/pbr3pp/5p1b/2KB3n/1N2N3/3P1PB1/PPP1P1PP/R2Q3R w - -", "d5c6",
     -300, "-B + B - N"},
    {"5k2/p2P2pp/8/1pb5/1Nn1P1n1/6Q1/PPP4P/R3K1NR w KQ -", "d7d8q", 800,
     "(Q - P)"},
    {"r4k2/p2P2pp/8/1pb5/1Nn1P1n1/6Q1/PPP4P/R3K1NR w KQ -", "d7d8q", -100,
     "(Q - P) - Q"},
    {"5k2/p2P2pp/1b6/1p6/1Nn1P1n1/8/PPP4P/R2QK1NR w KQ -", "d7d8q", 200,
     "(Q - P) - Q + B"},
    {"4kbnr/p1P1pppp/b7/4q3/7n/8/PP1PPPPP/RNBQKBNR w KQk -", "c7c8q", -100,
     "(Q - P) - Q"},
    {"4kbnr/p1P1pppp/b7/4q3/7n/8/PPQPPPPP/RNB1KBNR w KQk -", "c7c8q", 200,
     "(Q - P) - Q + B"},
    {"4kbnr/p1P1pppp/b7/4q3/7n/8/PPQPPPPP/RNB1KBNR w KQk -", "c7c8q", 200,
     "(Q - P)"},
    {"4kbnr/p1P4p/b1q5/5pP1/4n3/5Q2/PP1PPP1P/RNB1KBNR w KQk f6", "g5f6", 0,
     "P - P"},
    {"4kbnr/p1P4p/b1q5/5pP1/4n3/5Q2/PP1PPP1P/RNB1KBNR w KQk f6", "g5f6", 0,
     "P - P"},
    {"4kbnr/p1P4p/b1q5/5pP1/4n2Q/8/PP1PPP1P/RNB1KBNR w KQk f6", "g5f6", 0,
     "P - P"},
    {"1n2kb1r/p1P4p/2qb4/5pP1/4n2Q/8/PP1PPP1P/RNB1KBNR w KQk -", "c7b8q", 200,
     "N + (Q - P) - Q"},
    {"rnbqk2r/pp3ppp/2p1pn2/3p4/3P4/N1P1BN2/PPB1PPPb/R2Q1RK1 w kq -", "g1h2",
     300, "B"},
    {"3N4/2K5/2n5/1k6/8/8/8/8 b - -", "c6d8", 0, "N - N"},
    {"3n3r/2P5/8/1k6/8/8/3Q4/4K3 w - -", "c7d8q", 700, "(N + Q - P) - Q + R"},
    {"r2n3r/2P1P3/4N3/1k6/8/8/8/4K3 w - -", "e6d8", 300, "N"},
    {"8/8/8/1k6/6b1/4N3/2p3K1/3n4 w - -", "e3d1", 0, "N - N"},
    {"8/8/1k6/8/8/2N1N3/4p1K1/3n4 w - -", "c3d1", 100, "N - (N + Q - P) + Q"},
    {"r1bqk1nr/pppp1ppp/2n5/1B2p3/1b2P3/5N2/PPPP1PPP/RNBQK2R w KQkq -", "e1g1",
     0, ""}};

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

#define start_position                                                         \
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
char promoted_pieces[] = {[Q] = 'q', [R] = 'r', [B] = 'b', [N] = 'n',
                          [q] = 'q', [r] = 'r', [b] = 'b', [n] = 'n'};

//  parse user/GUI move string input (e.g. "e7e8q")
static inline int parse_move(position_t *pos, char *move_string) {
  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(pos, move_list);

  // parse source square
  int source_square = (move_string[0] - 'a') + (8 - (move_string[1] - '0')) * 8;
  // parse target square
  int target_square = (move_string[2] - 'a') + (8 - (move_string[3] - '0')) * 8;

  // loop over the moves within a move list
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // init move
    int move = move_list->entry[move_count].move;

    // make sure source & target squares are available within the generated move
    if (source_square == get_move_source(move) &&
        target_square == get_move_target(move)) {
      // init promoted piece
      int promoted_piece = get_move_promoted(move);

      // promoted piece is available
      if (promoted_piece) {
        // promoted to queen
        if ((promoted_piece == Q || promoted_piece == q) &&
            move_string[4] == 'q')
          // return legal move
          return move;

        // promoted to rook
        else if ((promoted_piece == R || promoted_piece == r) &&
                 move_string[4] == 'r')
          // return legal move
          return move;

        // promoted to bishop
        else if ((promoted_piece == B || promoted_piece == b) &&
                 move_string[4] == 'b')
          // return legal move
          return move;

        // promoted to knight
        else if ((promoted_piece == N || promoted_piece == n) &&
                 move_string[4] == 'n')
          // return legal move
          return move;

        // continue the loop on possible wrong promotions (e.g. "e7e8f")
        continue;
      }

      // return legal move
      return move;
    }
  }

  // return illegal move
  return 0;
}

static inline void reset_board(position_t *pos) {
  // reset board position (bitboards)
  memset(pos->bitboards, 0ULL, sizeof(pos->bitboards));

  // reset occupancies (bitboards)
  memset(pos->occupancies, 0ULL, sizeof(pos->occupancies));

  // reset game state variables
  pos->side = 0;
  pos->enpassant = no_sq;
  pos->castle = 0;

  // reset repetition index
  pos->repetition_index = 0;

  pos->fifty = 0;

  // reset repetition table
  memset(pos->repetition_table, 0ULL, sizeof(pos->repetition_table));
}

static inline void parse_fen(position_t *pos, char *fen) {
  // prepare for new game
  reset_board(pos);

  // loop over board ranks
  for (int rank = 0; rank < 8; rank++) {
    // loop over board files
    for (int file = 0; file < 8; file++) {
      // init current square
      int square = rank * 8 + file;

      // match ascii pieces within FEN string
      if ((*fen >= 'a' && *fen <= 'z') || (*fen >= 'A' && *fen <= 'Z')) {
        // init piece type
        int piece = char_pieces[*(uint8_t *)fen];

        // set piece on corresponding bitboard
        set_bit(pos->bitboards[piece], square);
        pos->mailbox[square] = piece;
        // increment pointer to FEN string
        fen++;
      }

      // match empty square numbers within FEN string
      if (*fen >= '0' && *fen <= '9') {
        // init offset (convert char 0 to int 0)
        int offset = *fen - '0';

        // define piece variable
        int piece = -1;

        uint8_t bb_piece = pos->mailbox[square];
        // if there is a piece on current square
        if (bb_piece != NO_PIECE && get_bit(pos->bitboards[bb_piece], square))
          // get piece code
          piece = bb_piece;

        // on empty current square
        if (piece == -1)
          // decrement file
          file--;

        // adjust file counter
        file += offset;

        // increment pointer to FEN string
        fen++;
      }

      // match rank separator
      if (*fen == '/')
        // increment pointer to FEN string
        fen++;
    }
  }

  // got to parsing side to move (increment pointer to FEN string)
  fen++;

  // parse side to move
  (*fen == 'w') ? (pos->side = white) : (pos->side = black);

  // go to parsing castling rights
  fen += 2;

  // parse castling rights
  while (*fen != ' ') {
    switch (*fen) {
    case 'K':
      pos->castle |= wk;
      break;
    case 'Q':
      pos->castle |= wq;
      break;
    case 'k':
      pos->castle |= bk;
      break;
    case 'q':
      pos->castle |= bq;
      break;
    case '-':
      break;
    }

    // increment pointer to FEN string
    fen++;
  }

  // got to parsing enpassant square (increment pointer to FEN string)
  fen++;

  // parse enpassant square
  if (*fen != '-') {
    // parse enpassant file & rank
    int file = fen[0] - 'a';
    int rank = 8 - (fen[1] - '0');

    // init enpassant square
    pos->enpassant = rank * 8 + file;
  }

  // no enpassant square
  else
    pos->enpassant = no_sq;

  // go to parsing half move counter (increment pointer to FEN string)
  fen++;

  // parse half move counter to init fifty move counter
  pos->fifty = atoi(fen);

  // loop over white pieces bitboards
  for (int piece = P; piece <= K; piece++)
    // populate white occupancy bitboard
    pos->occupancies[white] |= pos->bitboards[piece];

  // loop over black pieces bitboards
  for (int piece = p; piece <= k; piece++)
    // populate white occupancy bitboard
    pos->occupancies[black] |= pos->bitboards[piece];

  // init all occupancies
  pos->occupancies[both] |= pos->occupancies[white];
  pos->occupancies[both] |= pos->occupancies[black];

  // init hash key
  pos->hash_key = generate_hash_key(pos);
}

// parse UCI "position" command
static inline void parse_position(position_t *pos,
                                  char *command) {
  // shift pointer to the right where next token begins
  command += 9;

  // init pointer to the current character in the command string
  char *current_char = command;

  for (int i = 0; i < 64; ++i) {
    pos->mailbox[i] = NO_PIECE;
  }

  // parse UCI "startpos" command
  if (strncmp(command, "startpos", 8) == 0)
    // init chess board with start position
    parse_fen(pos, start_position);

  // parse UCI "fen" command
  else {
    // make sure "fen" command is available within command string
    current_char = strstr(command, "fen");

    // if no "fen" command is available within command string
    if (current_char == NULL)
      // init chess board with start position
      parse_fen(pos, start_position);

    // found "fen" substring
    else {
      // shift pointer to the right where next token begins
      current_char += 4;

      // init chess board with position from FEN string
      parse_fen(pos, current_char);
    }
  }

  init_accumulator(pos);

  // parse moves after position
  current_char = strstr(command, "moves");

  // moves available
  if (current_char != NULL) {
    // shift pointer to the right where next token begins
    current_char += 6;

    // loop over moves within a move string
    while (*current_char) {
      // parse next move
      int move = parse_move(pos, current_char);

      // if no more moves
      if (move == 0)
        // break out of the loop
        break;

      // increment repetition index
      pos->repetition_index++;

      // write hash key into a repetition table
      pos->repetition_table[pos->repetition_index] = pos->hash_key;

      // make move on the chess board
      make_move(pos, move, all_moves);

      // move current character pointer to the end of current move
      while (*current_char && *current_char != ' ')
        current_char++;

      // go to the next move
      current_char++;
    }
  }
}

static inline void time_control(position_t *pos, thread_t *threads,
                                char *line) {
  // reset time control
  threads->stopped = 0;
  threads->quit = 0;
  threads->starttime = 0;
  threads->stoptime = 0;
  threads->timeset = 0;
  memset(&limits, 0, sizeof(limits_t));

  // Default to 1/20 of the time to spend
  limits.movestogo = 20;

  threads[0].starttime = get_time_ms();

  // init argument
  char *argument = NULL;

  // infinite search
  if ((argument = strstr(line, "infinite"))) {
  }

  if (pos->side == white) {
    if ((argument = strstr(line, "winc"))) {
      limits.inc = atoi(argument + 5);
    }
    if ((argument = strstr(line, "wtime"))) {
      limits.time = atoi(argument + 6);
    }
  } else {
    if ((argument = strstr(line, "binc"))) {
      limits.inc = atoi(argument + 5);
    }
    if ((argument = strstr(line, "btime"))) {
      limits.time = atoi(argument + 6);
    }
  }
  // match UCI "movestogo" command
  if ((argument = strstr(line, "movestogo")))
    // parse number of moves to go
    limits.movestogo = atoi(argument + 10);

  // match UCI "movetime" command
  if ((argument = strstr(line, "movetime"))) {
    // parse amount of time allowed to spend to make a move
    limits.time = atoi(argument + 9);
    limits.movestogo = 1;
  }
  // match UCI "depth" command
  if ((argument = strstr(line, "depth"))) {
    // parse search depth
    limits.depth = atoi(argument + 6);
  }

  if ((argument = strstr(line, "perft"))) {
    limits.depth = atoi(argument + 6);
    perft_test(pos, threads, limits.depth);
  } else {
    limits.depth = limits.depth == 0 ? max_ply : limits.depth;

    if (limits.time) {
      int64_t time_this_move = (limits.time / limits.movestogo) + limits.inc;
      int64_t max_time = limits.time;
      threads->stoptime =
          threads->starttime + MIN(max_time, time_this_move) - 50;
      threads->timeset = 1;
    } else {
      threads->timeset = 0;
    }
  }
}

static inline void *parse_go(void *searchthread_info) {
  searchthreadinfo_t *sti = (searchthreadinfo_t *)searchthread_info;
  position_t *pos = sti->pos;
  thread_t *threads = sti->threads;
  char *line = sti->line;

  time_control(pos, threads, line);

  search_position(pos, threads);
  return NULL;
}

// print move (for UCI purposes)
void print_move(int move) {
  if (get_move_promoted(move))
    printf("%s%s%c", square_to_coordinates[get_move_source(move)],
           square_to_coordinates[get_move_target(move)],
           promoted_pieces[get_move_promoted(move)]);
  else
    printf("%s%s", square_to_coordinates[get_move_source(move)],
           square_to_coordinates[get_move_target(move)]);
}

// main UCI loop
void uci_loop(position_t *pos, thread_t *threads, int argc, char *argv[]) {
  // max hash MB
  int max_hash = 65536;

  pthread_t search_thread;
  searchthreadinfo_t sti;
  sti.threads = threads;
  sti.pos = pos;

// reset STDIN & STDOUT buffers
#ifndef WIN64
  setbuf(stdin, NULL);
#endif
  setbuf(stdout, NULL);

  // define user / GUI input buffer
  char input[10000];

  // print engine info
  printf("Quanticade %s by DarkNeutrino\n", version);

  // Setup engine with start position as default
  parse_position(pos, "position startpos");
  init_accumulator(pos);

  if (argc >= 2) {
    if (strncmp("bench", argv[1], 5) == 0) {
      uint64_t total_nodes = 0;
      uint64_t start_time = get_time_ms();
      for (int pos_index = 0; pos_index < 50; ++pos_index) {
        printf("\nPosition %d/%d (%s)\n", pos_index, 49,
               bench_positions[pos_index]);
        parse_fen(pos, bench_positions[pos_index]);
        time_control(pos, threads, "go depth 12");
        search_position(pos, threads);
        total_nodes += threads->nodes;
      }
      uint64_t total_time = get_time_ms() - start_time;
      printf("\n%" PRIu64 " nodes %" PRIu64 " nps\n", total_nodes,
             (total_nodes / (total_time + 1) * 1000));
      return;
    }
  }

  for (int index = 0; index < 71; ++index) {
    printf("Position: %s with move %s index %d score %d explanation %s\n", see_pos[index].fen, see_pos[index].move, index, see_pos[index].score, see_pos[index].explanation);
    parse_fen(pos, see_pos[index].fen);
    int move = parse_move(pos, see_pos[index].move);
    if ((SEE(pos, move, see_pos[index].score) && !SEE(pos, move, see_pos[index].score + 1)) || get_move_promoted(move)) {
      printf("SEE Test passed\n");
    }
    else {
      int threshold;
      for (int i = -2000; i < 2000; ++i) {
        if (SEE(pos, move, i)) {
          threshold = i;
        }
      }
      printf("FAILED, passes with %d\n", threshold);
    }
  }
  return;

  // main loop
  while (1) {
    // reset user /GUI input
    memset(input, 0, sizeof(input));

    // make sure output reaches the GUI
    fflush(stdout);

    // get user / GUI input
    if (!fgets(input, 10000, stdin))
      // continue the loop
      continue;

    // make sure input is available
    if (input[0] == '\n')
      // continue the loop
      continue;

    // parse UCI "isready" command
    if (strncmp(input, "isready", 7) == 0) {
      printf("readyok\n");
      continue;
    }

    // parse UCI "position" command
    else if (strncmp(input, "position", 8) == 0) {
      // call parse position function
      parse_position(pos, input);
      init_accumulator(pos);
    }
    // parse UCI "ucinewgame" command
    else if (strncmp(input, "ucinewgame", 10) == 0) {
      // clear hash table
      clear_hash_table();
      for (int i = 0; i < thread_count; ++i) {
        memset(threads[i].history_moves, 0, sizeof(threads[i].history_moves));
      }
    }
    // parse UCI "go" command
    else if (strncmp(input, "go", 2) == 0) {
      // call parse go function
      if (nnue_settings.use_nnue) {
        printf("info string NNUE evaluation using %s\n",
               nnue_settings.nnue_file);
      }
      strncpy(sti.line, input, 10000);
      pthread_create(&search_thread, NULL, &parse_go, &sti);
    }

    else if (strncmp(input, "stop", 4) == 0) {
      threads->stopped = 1;
      pthread_join(search_thread, NULL);
    }
    // parse UCI "quit" command
    else if (strncmp(input, "quit", 4) == 0)
      // quit from the UCI loop (terminate program)
      break;

    // parse UCI "uci" command
    else if (strncmp(input, "uci", 3) == 0) {
      // print engine info
      printf("id name Quanticade %s\n", version);
      printf("id author DarkNeutrino\n\n");
      printf("option name Hash type spin default %d min 4 max %d\n",
             default_hash_size, max_hash);
      printf("option name Threads type spin default %d min %d max %d\n", 1, 1,
             256);
      printf("option name Use NNUE type check default true\n");
      printf("option name EvalFile type string default %s\n",
             nnue_settings.nnue_file);
      printf("option name Clear Hash type button\n");
      printf("option name LMP_BASE type spin default 6 min 1 max 12\n");
      printf("option name LMP_MULTIPLIER type spin default 2 min 1 max 4\n");
      printf("option name RAZOR_DEPTH type spin default 7 min 1 max 14\n");
      printf("option name RAZOR_MARGIN type spin default 298 min 1 max 586\n");
      printf("option name RFP_DEPTH type spin default 6 min 1 max 12\n");
      printf("option name RFP_MARGIN type spin default 120 min 1 max 240\n");
      printf(
          "option name NMP_BASE_REDUCTION type spin default 5 min 1 max 10\n");
      printf("option name NMP_DIVISOR type spin default 9 min 1 max 18\n");
      printf("option name NMP_DEPTH type spin default 1 min 1 max 4\n");
      printf("option name IID_DEPTH type spin default 4 min 1 max 8\n");
      printf("option name IID_REDUCTION type spin default 4 min 1 max 8\n");
      // printf("option name SyzygyPath type string default <empty>\n");
      printf("uciok\n");
    } else if (strncmp(input, "spsa", 4) == 0) {
      printf("LMP_BASE, int, %.3f, 1.000, %.3f, %.3f, 0.002\n", (float)LMP_BASE,
             (float)LMP_BASE * 2,
             MAX(0.5, MAX(1, (((float)LMP_BASE * 2) - 1)) / 20));
      printf("LMP_MULTIPLIER, int, %.3f, 1.000, %.3f, %.3f, 0.002\n",
             (float)LMP_MULTIPLIER, (float)LMP_MULTIPLIER * 2,
             MAX(0.5, MAX(1, (((float)LMP_MULTIPLIER * 2) - 1)) / 20));
      printf("RAZOR_DEPTH, int, %.3f, 1.000, %.3f, %.3f, 0.002\n",
             (float)RAZOR_DEPTH, (float)RAZOR_DEPTH * 2,
             MAX(0.5, MAX(1, (((float)RAZOR_DEPTH * 2) - 1)) / 20));
      printf("RAZOR_MARGIN, int, %.3f, 1.000, %.3f, %.3f, 0.002\n",
             (float)RAZOR_MARGIN, (float)RAZOR_MARGIN * 2,
             MAX(0.5, MAX(1, (((float)RAZOR_MARGIN * 2) - 1)) / 20));
      printf("RFP_DEPTH, int, %.3f, 1.000, %.3f, %.3f, 0.002\n",
             (float)RFP_DEPTH, (float)RFP_DEPTH * 2,
             MAX(0.5, MAX(1, (((float)RFP_DEPTH * 2) - 1)) / 20));
      printf("RFP_MARGIN, int, %.3f, 1.000, %.3f, %.3f, 0.002\n",
             (float)RFP_MARGIN, (float)RFP_MARGIN * 2,
             MAX(0.5, MAX(1, (((float)RFP_MARGIN * 2) - 1)) / 20));
      printf("NMP_BASE_REDUCTION, int, %.3f, 1.000, %.3f, %.3f, 0.002\n",
             (float)NMP_BASE_REDUCTION, (float)NMP_BASE_REDUCTION * 2,
             MAX(0.5, MAX(1, (((float)NMP_BASE_REDUCTION * 2) - 1)) / 20));
      printf("NMP_DIVISOR, int, %.3f, 1.000, %.3f, %.3f, 0.002\n",
             (float)NMP_DIVISER, (float)NMP_DIVISER * 2,
             MAX(0.5, MAX(1, (((float)NMP_DIVISER * 2) - 1)) / 20));
      printf("NMP_DEPTH, int, %.3f, 1.000, %.3f, %.3f, 0.002\n",
             (float)NMP_DEPTH, (float)4, 0.5);
      printf("IID_DEPTH, int, %.3f, 1.000, %.3f, %.3f, 0.002\n",
             (float)IID_DEPTH, (float)IID_DEPTH * 2,
             MAX(0.5, MAX(1, (((float)IID_DEPTH * 2) - 1)) / 20));
      printf("IID_REDUCTION, int, %.3f, 1.000, %.3f, %.3f, 0.002\n",
             (float)IID_REDUCTION, (float)IID_REDUCTION * 2,
             MAX(0.5, MAX(1, (((float)IID_REDUCTION * 2) - 1)) / 20));
    }

    else if (!strncmp(input, "setoption name Hash value ", 26)) {
      int mb = 0;
      // init MB
      sscanf(input, "%*s %*s %*s %*s %d", &mb);

      // adjust MB if going beyond the allowed bounds
      if (mb < 4)
        mb = 4;
      if (mb > max_hash)
        mb = max_hash;

      // set hash table size in MB
      printf("    Set hash table size to %dMB\n", mb);
      init_hash_table(mb);
    }

    else if (!strncmp(input, "setoption name Use NNUE value ", 30)) {
      char *use_nnue;
      uint16_t length = strlen(input);
      use_nnue = calloc(length - 30, 1);
      sscanf(input, "%*s %*s %*s %*s %s", use_nnue);

      if (strncmp(use_nnue, "true", 4) == 0) {
        nnue_settings.use_nnue = 1;
      } else {
        nnue_settings.use_nnue = 0;
      }
    }

    else if (!strncmp(input, "setoption name Threads value ", 29)) {
      sscanf(input, "%*s %*s %*s %*s %d", &thread_count);
      free(threads);
      threads = init_threads(thread_count);
      sti.threads = threads;
    }

    else if (!strncmp(input, "setoption name EvalFile value ", 30)) {
      free(nnue_settings.nnue_file);
      uint16_t length = strlen(input);
      nnue_settings.nnue_file = calloc(length - 30, 1);
      sscanf(input, "%*s %*s %*s %*s %s", nnue_settings.nnue_file);
      nnue_init(nnue_settings.nnue_file);
    }

    else if (!strncmp(input, "setoption name Clear Hash", 25)) {
      clear_hash_table();
    } else if (!strncmp(input, "setoption name SyzygyPath value ", 32)) {
      char *ptr = input + 32;
      // tb_init(ptr);
      printf("info string set SyzygyPath to %s\n", ptr);
    } else if (!strncmp(input, "setoption name LMP_BASE value ", 30)) {
      sscanf(input, "%*s %*s %*s %*s %d", &LMP_BASE);
    } else if (!strncmp(input, "setoption name LMP_MULTIPLIER value ", 36)) {
      sscanf(input, "%*s %*s %*s %*s %d", &LMP_MULTIPLIER);
    } else if (!strncmp(input, "setoption name RAZOR_DEPTH value ", 33)) {
      sscanf(input, "%*s %*s %*s %*s %d", &RAZOR_DEPTH);
    } else if (!strncmp(input, "setoption name RAZOR_MARGIN value ", 34)) {
      sscanf(input, "%*s %*s %*s %*s %d", &RAZOR_MARGIN);
    } else if (!strncmp(input, "setoption name RFP_DEPTH value ", 31)) {
      sscanf(input, "%*s %*s %*s %*s %d", &RFP_DEPTH);
    } else if (!strncmp(input, "setoption name RFP_MARGIN value ", 32)) {
      sscanf(input, "%*s %*s %*s %*s %d", &RFP_MARGIN);
    } else if (!strncmp(input, "setoption name NMP_BASE_REDUCTION value ",
                        40)) {
      sscanf(input, "%*s %*s %*s %*s %d", &NMP_BASE_REDUCTION);
    } else if (!strncmp(input, "setoption name NMP_DIVISOR value ", 33)) {
      sscanf(input, "%*s %*s %*s %*s %d", &NMP_DIVISER);
    } else if (!strncmp(input, "setoption name NMP_DEPTH value ", 31)) {
      sscanf(input, "%*s %*s %*s %*s %d", &NMP_DEPTH);
    } else if (!strncmp(input, "setoption name IID_DEPTH value ", 31)) {
      sscanf(input, "%*s %*s %*s %*s %d", &IID_DEPTH);
    } else if (!strncmp(input, "setoption name IID_REDUCTION value ", 35)) {
      sscanf(input, "%*s %*s %*s %*s %d", &IID_REDUCTION);
    }
  }
}
