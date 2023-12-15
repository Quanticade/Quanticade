#ifndef CONSTS_H
#define CONSTS_H

#include "enums.h"

#define start_position                                                         \
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 "

// convert squares to coordinates
const char *square_to_coordinates[] = {
    "a8", "b8", "c8", "d8", "e8", "f8", "g8", "h8", "a7", "b7", "c7",
    "d7", "e7", "f7", "g7", "h7", "a6", "b6", "c6", "d6", "e6", "f6",
    "g6", "h6", "a5", "b5", "c5", "d5", "e5", "f5", "g5", "h5", "a4",
    "b4", "c4", "d4", "e4", "f4", "g4", "h4", "a3", "b3", "c3", "d3",
    "e3", "f3", "g3", "h3", "a2", "b2", "c2", "d2", "e2", "f2", "g2",
    "h2", "a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1",
};

// ASCII pieces
char ascii_pieces[12] = "PNBRQKpnbrqk";

// unicode pieces
char *unicode_pieces[12] = {"♙", "♘", "♗", "♖", "♕", "♔",
                            "♟︎", "♞", "♝", "♜", "♛", "♚"};

// convert ASCII character pieces to encoded constants
int char_pieces[] = {
    ['P'] = P, ['N'] = N, ['B'] = B, ['R'] = R, ['Q'] = Q, ['K'] = K,
    ['p'] = p, ['n'] = n, ['b'] = b, ['r'] = r, ['q'] = q, ['k'] = k};

// promoted pieces
char promoted_pieces[] = {[Q] = 'q', [R] = 'r', [B] = 'b', [N] = 'n',
                          [q] = 'q', [r] = 'r', [b] = 'b', [n] = 'n'};

#endif
