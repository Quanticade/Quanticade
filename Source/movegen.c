#include "movegen.h"
#include "attacks.h"
#include "bitboards.h"
#include "enums.h"
#include "structs.h"
#include <string.h>

const int castling_rights[64] = {
    7,  15, 15, 15, 3,  15, 15, 11, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 13, 15, 15, 15, 12, 15, 15, 14};

int make_move(engine_t *engine, int move, int move_flag) {
  // quiet moves
  if (move_flag == all_moves) {
    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    // parse move
    int source_square = get_move_source(move);
    int target_square = get_move_target(move);
    int piece = get_move_piece(move);
    int promoted_piece = get_move_promoted(move);
    int capture = get_move_capture(move);
    int double_push = get_move_double(move);
    int enpass = get_move_enpassant(move);
    int castling = get_move_castling(move);

    // move piece
    pop_bit(engine->board.bitboards[piece], source_square);
    set_bit(engine->board.bitboards[piece], target_square);

    // hash piece
    engine->board.hash_key ^=
        engine->keys
            .piece_keys[piece][source_square]; // remove piece from source
                                               // square in hash key
    engine->board.hash_key ^=
        engine->keys
            .piece_keys[piece][target_square]; // set piece to the target square
                                               // in hash key

    // increment fifty move rule counter
    engine->fifty++;

    // if pawn moved
    if (piece == P || piece == p)
      // reset fifty move rule counter
      engine->fifty = 0;

    // handling capture moves
    if (capture) {
      // pick up bitboard piece index ranges depending on side
      int start_piece, end_piece;

      // reset fifty move rule counter
      engine->fifty = 0;

      // white to move
      if (engine->board.side == white) {
        start_piece = p;
        end_piece = k;
      }

      // black to move
      else {
        start_piece = P;
        end_piece = K;
      }

      // loop over bitboards opposite to the current side to move
      for (int bb_piece = start_piece; bb_piece <= end_piece; bb_piece++) {
        // if there's a piece on the target square
        if (get_bit(engine->board.bitboards[bb_piece], target_square)) {
          // remove it from corresponding bitboard
          pop_bit(engine->board.bitboards[bb_piece], target_square);

          // remove the piece from hash key
          engine->board.hash_key ^=
              engine->keys.piece_keys[bb_piece][target_square];
          break;
        }
      }
    }

    // handle pawn promotions
    if (promoted_piece) {
      // white to move
      if (engine->board.side == white) {
        // erase the pawn from the target square
        pop_bit(engine->board.bitboards[P], target_square);

        // remove pawn from hash key
        engine->board.hash_key ^= engine->keys.piece_keys[P][target_square];
      }

      // black to move
      else {
        // erase the pawn from the target square
        pop_bit(engine->board.bitboards[p], target_square);

        // remove pawn from hash key
        engine->board.hash_key ^= engine->keys.piece_keys[p][target_square];
      }

      // set up promoted piece on chess board
      set_bit(engine->board.bitboards[promoted_piece], target_square);

      // add promoted piece into the hash key
      engine->board.hash_key ^=
          engine->keys.piece_keys[promoted_piece][target_square];
    }

    // handle enpassant captures
    if (enpass) {
      // erase the pawn depending on side to move
      (engine->board.side == white)
          ? pop_bit(engine->board.bitboards[p], target_square + 8)
          : pop_bit(engine->board.bitboards[P], target_square - 8);

      // white to move
      if (engine->board.side == white) {
        // remove captured pawn
        pop_bit(engine->board.bitboards[p], target_square + 8);

        // remove pawn from hash key
        engine->board.hash_key ^= engine->keys.piece_keys[p][target_square + 8];
      }

      // black to move
      else {
        // remove captured pawn
        pop_bit(engine->board.bitboards[P], target_square - 8);

        // remove pawn from hash key
        engine->board.hash_key ^= engine->keys.piece_keys[P][target_square - 8];
      }
    }

    // hash enpassant if available (remove enpassant square from hash key)
    if (engine->board.enpassant != no_sq)
      engine->board.hash_key ^=
          engine->keys.enpassant_keys[engine->board.enpassant];

    // reset enpassant square
    engine->board.enpassant = no_sq;

    // handle double pawn push
    if (double_push) {
      // white to move
      if (engine->board.side == white) {
        // set enpassant square
        engine->board.enpassant = target_square + 8;

        // hash enpassant
        engine->board.hash_key ^=
            engine->keys.enpassant_keys[target_square + 8];
      }

      // black to move
      else {
        // set enpassant square
        engine->board.enpassant = target_square - 8;

        // hash enpassant
        engine->board.hash_key ^=
            engine->keys.enpassant_keys[target_square - 8];
      }
    }

    // handle castling moves
    if (castling) {
      // switch target square
      switch (target_square) {
      // white castles king side
      case (g1):
        // move H rook
        pop_bit(engine->board.bitboards[R], h1);
        set_bit(engine->board.bitboards[R], f1);

        // hash rook
        engine->board.hash_key ^=
            engine->keys.piece_keys[R][h1]; // remove rook from h1 from hash key
        engine->board.hash_key ^=
            engine->keys.piece_keys[R][f1]; // put rook on f1 into a hash key
        break;

      // white castles queen side
      case (c1):
        // move A rook
        pop_bit(engine->board.bitboards[R], a1);
        set_bit(engine->board.bitboards[R], d1);

        // hash rook
        engine->board.hash_key ^=
            engine->keys.piece_keys[R][a1]; // remove rook from a1 from hash key
        engine->board.hash_key ^=
            engine->keys.piece_keys[R][d1]; // put rook on d1 into a hash key
        break;

      // black castles king side
      case (g8):
        // move H rook
        pop_bit(engine->board.bitboards[r], h8);
        set_bit(engine->board.bitboards[r], f8);

        // hash rook
        engine->board.hash_key ^=
            engine->keys.piece_keys[r][h8]; // remove rook from h8 from hash key
        engine->board.hash_key ^=
            engine->keys.piece_keys[r][f8]; // put rook on f8 into a hash key
        break;

      // black castles queen side
      case (c8):
        // move A rook
        pop_bit(engine->board.bitboards[r], a8);
        set_bit(engine->board.bitboards[r], d8);

        // hash rook
        engine->board.hash_key ^=
            engine->keys.piece_keys[r][a8]; // remove rook from a8 from hash key
        engine->board.hash_key ^=
            engine->keys.piece_keys[r][d8]; // put rook on d8 into a hash key
        break;
      }
    }

    // hash castling
    engine->board.hash_key ^= engine->keys.castle_keys[engine->board.castle];

    // update castling rights
    engine->board.castle &= castling_rights[source_square];
    engine->board.castle &= castling_rights[target_square];

    // hash castling
    engine->board.hash_key ^= engine->keys.castle_keys[engine->board.castle];

    // reset occupancies
    memset(engine->board.occupancies, 0ULL, 24);

    // loop over white pieces bitboards
    for (int bb_piece = P; bb_piece <= K; bb_piece++)
      // update white occupancies
      engine->board.occupancies[white] |= engine->board.bitboards[bb_piece];

    // loop over black pieces bitboards
    for (int bb_piece = p; bb_piece <= k; bb_piece++)
      // update black occupancies
      engine->board.occupancies[black] |= engine->board.bitboards[bb_piece];

    // update both sides occupancies
    engine->board.occupancies[both] |= engine->board.occupancies[white];
    engine->board.occupancies[both] |= engine->board.occupancies[black];

    // change side
    engine->board.side ^= 1;

    // hash side
    engine->board.hash_key ^= engine->keys.side_key;

    // make sure that king has not been exposed into a check
    if (is_square_attacked(engine,
                           (engine->board.side == white)
                               ? __builtin_ctzll(engine->board.bitboards[k])
                               : __builtin_ctzll(engine->board.bitboards[K]),
                           engine->board.side)) {
      // take move back
      restore_board(engine->board.bitboards, engine->board.occupancies,
                    engine->board.side, engine->board.enpassant,
                    engine->board.castle, engine->fifty,
                    engine->board.hash_key);

      // return illegal move
      return 0;
    }

    // otherwise
    else
      // return legal move
      return 1;

  }

  // capture moves
  else {
    // make sure move is the capture
    if (get_move_capture(move))
      return make_move(engine, move, all_moves);

    // otherwise the move is not a capture
    else
      // don't make it
      return 0;
  }
}

// add move to the move list
static inline void add_move(moves *move_list, int move) {
  // store move
  move_list->entry[move_list->count].move = move;

  // increment move count
  move_list->count++;
}

void generate_captures(engine_t *engine, moves *move_list) {
  // init move count
  move_list->count = 0;

  // define source & target squares
  int source_square, target_square;

  // define current piece's bitboard copy & its attacks
  uint64_t bitboard, attacks;
  uint8_t side = engine->board.side;
  // loop over all the bitboards
  for (int piece = P; piece <= k; piece++) {
    // init piece bitboard copy
    bitboard = engine->board.bitboards[piece];

    // generate white pawns & white king castling moves
    if (engine->board.side == white) {
      // pick up white pawn bitboards index
      if (piece == P) {
        // loop over white pawns within white pawn bitboard
        while (bitboard) {
          // init source square
          source_square = __builtin_ctzll(bitboard);

          // init target square
          target_square = source_square - 8;

          // init pawn attacks bitboard
          attacks = pawn_attacks[side][source_square] &
                    engine->board.occupancies[black];

          // generate pawn captures
          while (attacks) {
            // init target square
            target_square = __builtin_ctzll(attacks);

            // pawn promotion
            if (source_square >= a7 && source_square <= h7) {
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, Q, 1, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, R, 1, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, B, 1, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, N, 1, 0, 0, 0));
            }

            else
              // one square ahead pawn move
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, 0, 1, 0, 0, 0));

            // pop ls1b of the pawn attacks
            pop_bit(attacks, target_square);
          }

          // generate enpassant captures
          if (engine->board.enpassant != no_sq) {
            // lookup pawn attacks and bitwise AND with enpassant square (bit)
            uint64_t enpassant_attacks = pawn_attacks[side][source_square] &
                                         (1ULL << engine->board.enpassant);

            // make sure enpassant capture available
            if (enpassant_attacks) {
              // init enpassant capture target square
              int target_enpassant = __builtin_ctzll(enpassant_attacks);
              add_move(move_list, encode_move(source_square, target_enpassant,
                                              piece, 0, 1, 0, 1, 0));
            }
          }

          // pop ls1b from piece bitboard copy
          pop_bit(bitboard, source_square);
        }
      }
    }

    // generate black pawns & black king castling moves
    else {
      // pick up black pawn bitboards index
      if (piece == p) {
        // loop over white pawns within white pawn bitboard
        while (bitboard) {
          // init source square
          source_square = __builtin_ctzll(bitboard);

          // init target square
          target_square = source_square + 8;

          // init pawn attacks bitboard
          attacks = pawn_attacks[side][source_square] &
                    engine->board.occupancies[white];

          // generate pawn captures
          while (attacks) {
            // init target square
            target_square = __builtin_ctzll(attacks);

            // pawn promotion
            if (source_square >= a2 && source_square <= h2) {
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, q, 1, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, r, 1, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, b, 1, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, n, 1, 0, 0, 0));
            }

            else
              // one square ahead pawn move
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, 0, 1, 0, 0, 0));

            // pop ls1b of the pawn attacks
            pop_bit(attacks, target_square);
          }

          // generate enpassant captures
          if (engine->board.enpassant != no_sq) {
            // lookup pawn attacks and bitwise AND with enpassant square (bit)
            uint64_t enpassant_attacks = pawn_attacks[side][source_square] &
                                         (1ULL << engine->board.enpassant);

            // make sure enpassant capture available
            if (enpassant_attacks) {
              // init enpassant capture target square
              int target_enpassant = __builtin_ctzll(enpassant_attacks);
              add_move(move_list, encode_move(source_square, target_enpassant,
                                              piece, 0, 1, 0, 1, 0));
            }
          }

          // pop ls1b from piece bitboard copy
          pop_bit(bitboard, source_square);
        }
      }
    }

    // genarate knight moves
    if ((side == white) ? piece == N : piece == n) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = knight_attacks[source_square] &
                  ((side == white) ? ~engine->board.occupancies[white]
                                   : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (get_bit(((side == white) ? engine->board.occupancies[black]
                                       : engine->board.occupancies[white]),
                      target_square))
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 1, 0, 0, 0));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate bishop moves
    if ((side == white) ? piece == B : piece == b) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = get_bishop_attacks(source_square,
                                     engine->board.occupancies[both]) &
                  ((side == white) ? ~engine->board.occupancies[white]
                                   : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (get_bit(((side == white) ? engine->board.occupancies[black]
                                       : engine->board.occupancies[white]),
                      target_square))
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 1, 0, 0, 0));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate rook moves
    if ((side == white) ? piece == R : piece == r) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = get_rook_attacks(source_square,
                                   engine->board.occupancies[both]) &
                  ((side == white) ? ~engine->board.occupancies[white]
                                   : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (get_bit(((side == white) ? engine->board.occupancies[black]
                                       : engine->board.occupancies[white]),
                      target_square))
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 1, 0, 0, 0));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate queen moves
    if ((side == white) ? piece == Q : piece == q) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = get_queen_attacks(source_square,
                                    engine->board.occupancies[both]) &
                  ((side == white) ? ~engine->board.occupancies[white]
                                   : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (get_bit(((side == white) ? engine->board.occupancies[black]
                                       : engine->board.occupancies[white]),
                      target_square))
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 1, 0, 0, 0));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate king moves
    if ((side == white) ? piece == K : piece == k) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = king_attacks[source_square] &
                  ((side == white) ? ~engine->board.occupancies[white]
                                   : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (get_bit(((side == white) ? engine->board.occupancies[black]
                                       : engine->board.occupancies[white]),
                      target_square))
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 1, 0, 0, 0));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }
  }
}

// generate all moves
void generate_moves(engine_t *engine, moves *move_list) {
  // init move count
  move_list->count = 0;

  // define source & target squares
  int source_square, target_square;

  // define current piece's bitboard copy & its attacks
  uint64_t bitboard, attacks;

  // loop over all the bitboards
  for (int piece = P; piece <= k; piece++) {
    // init piece bitboard copy
    bitboard = engine->board.bitboards[piece];

    // generate white pawns & white king castling moves
    if (engine->board.side == white) {
      // pick up white pawn bitboards index
      if (piece == P) {
        // loop over white pawns within white pawn bitboard
        while (bitboard) {
          // init source square
          source_square = __builtin_ctzll(bitboard);

          // init target square
          target_square = source_square - 8;

          // generate quiet pawn moves
          if (!(target_square < a8) &&
              !get_bit(engine->board.occupancies[both], target_square)) {
            // pawn promotion
            if (source_square >= a7 && source_square <= h7) {
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, Q, 0, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, R, 0, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, B, 0, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, N, 0, 0, 0, 0));
            }

            else {
              // one square ahead pawn move
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, 0, 0, 0, 0, 0));

              // two squares ahead pawn move
              if ((source_square >= a2 && source_square <= h2) &&
                  !get_bit(engine->board.occupancies[both], target_square - 8))
                add_move(move_list,
                         encode_move(source_square, (target_square - 8), piece,
                                     0, 0, 1, 0, 0));
            }
          }

          // init pawn attacks bitboard
          attacks = pawn_attacks[engine->board.side][source_square] &
                    engine->board.occupancies[black];

          // generate pawn captures
          while (attacks) {
            // init target square
            target_square = __builtin_ctzll(attacks);

            // pawn promotion
            if (source_square >= a7 && source_square <= h7) {
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, Q, 1, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, R, 1, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, B, 1, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, N, 1, 0, 0, 0));
            }

            else
              // one square ahead pawn move
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, 0, 1, 0, 0, 0));

            // pop ls1b of the pawn attacks
            pop_bit(attacks, target_square);
          }

          // generate enpassant captures
          if (engine->board.enpassant != no_sq) {
            // lookup pawn attacks and bitwise AND with enpassant square (bit)
            uint64_t enpassant_attacks =
                pawn_attacks[engine->board.side][source_square] &
                (1ULL << engine->board.enpassant);

            // make sure enpassant capture available
            if (enpassant_attacks) {
              // init enpassant capture target square
              int target_enpassant = __builtin_ctzll(enpassant_attacks);
              add_move(move_list, encode_move(source_square, target_enpassant,
                                              piece, 0, 1, 0, 1, 0));
            }
          }

          // pop ls1b from piece bitboard copy
          pop_bit(bitboard, source_square);
        }
      }

      // castling moves
      if (piece == K) {
        // king side castling is available
        if (engine->board.castle & wk) {
          // make sure square between king and king's rook are empty
          if (!get_bit(engine->board.occupancies[both], f1) &&
              !get_bit(engine->board.occupancies[both], g1)) {
            // make sure king and the f1 squares are not under attacks
            if (!is_square_attacked(engine, e1, black) &&
                !is_square_attacked(engine, f1, black))
              add_move(move_list, encode_move(e1, g1, piece, 0, 0, 0, 0, 1));
          }
        }

        // queen side castling is available
        if (engine->board.castle & wq) {
          // make sure square between king and queen's rook are empty
          if (!get_bit(engine->board.occupancies[both], d1) &&
              !get_bit(engine->board.occupancies[both], c1) &&
              !get_bit(engine->board.occupancies[both], b1)) {
            // make sure king and the d1 squares are not under attacks
            if (!is_square_attacked(engine, e1, black) &&
                !is_square_attacked(engine, d1, black))
              add_move(move_list, encode_move(e1, c1, piece, 0, 0, 0, 0, 1));
          }
        }
      }
    }

    // generate black pawns & black king castling moves
    else {
      // pick up black pawn bitboards index
      if (piece == p) {
        // loop over white pawns within white pawn bitboard
        while (bitboard) {
          // init source square
          source_square = __builtin_ctzll(bitboard);

          // init target square
          target_square = source_square + 8;

          // generate quiet pawn moves
          if (!(target_square > h1) &&
              !get_bit(engine->board.occupancies[both], target_square)) {
            // pawn promotion
            if (source_square >= a2 && source_square <= h2) {
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, q, 0, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, r, 0, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, b, 0, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, n, 0, 0, 0, 0));
            }

            else {
              // one square ahead pawn move
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, 0, 0, 0, 0, 0));

              // two squares ahead pawn move
              if ((source_square >= a7 && source_square <= h7) &&
                  !get_bit(engine->board.occupancies[both], target_square + 8))
                add_move(move_list,
                         encode_move(source_square, (target_square + 8), piece,
                                     0, 0, 1, 0, 0));
            }
          }

          // init pawn attacks bitboard
          attacks = pawn_attacks[engine->board.side][source_square] &
                    engine->board.occupancies[white];

          // generate pawn captures
          while (attacks) {
            // init target square
            target_square = __builtin_ctzll(attacks);

            // pawn promotion
            if (source_square >= a2 && source_square <= h2) {
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, q, 1, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, r, 1, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, b, 1, 0, 0, 0));
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, n, 1, 0, 0, 0));
            }

            else
              // one square ahead pawn move
              add_move(move_list, encode_move(source_square, target_square,
                                              piece, 0, 1, 0, 0, 0));

            // pop ls1b of the pawn attacks
            pop_bit(attacks, target_square);
          }

          // generate enpassant captures
          if (engine->board.enpassant != no_sq) {
            // lookup pawn attacks and bitwise AND with enpassant square (bit)
            uint64_t enpassant_attacks =
                pawn_attacks[engine->board.side][source_square] &
                (1ULL << engine->board.enpassant);

            // make sure enpassant capture available
            if (enpassant_attacks) {
              // init enpassant capture target square
              int target_enpassant = __builtin_ctzll(enpassant_attacks);
              add_move(move_list, encode_move(source_square, target_enpassant,
                                              piece, 0, 1, 0, 1, 0));
            }
          }

          // pop ls1b from piece bitboard copy
          pop_bit(bitboard, source_square);
        }
      }

      // castling moves
      if (piece == k) {
        // king side castling is available
        if (engine->board.castle & bk) {
          // make sure square between king and king's rook are empty
          if (!get_bit(engine->board.occupancies[both], f8) &&
              !get_bit(engine->board.occupancies[both], g8)) {
            // make sure king and the f8 squares are not under attacks
            if (!is_square_attacked(engine, e8, white) &&
                !is_square_attacked(engine, f8, white))
              add_move(move_list, encode_move(e8, g8, piece, 0, 0, 0, 0, 1));
          }
        }

        // queen side castling is available
        if (engine->board.castle & bq) {
          // make sure square between king and queen's rook are empty
          if (!get_bit(engine->board.occupancies[both], d8) &&
              !get_bit(engine->board.occupancies[both], c8) &&
              !get_bit(engine->board.occupancies[both], b8)) {
            // make sure king and the d8 squares are not under attacks
            if (!is_square_attacked(engine, e8, white) &&
                !is_square_attacked(engine, d8, white))
              add_move(move_list, encode_move(e8, c8, piece, 0, 0, 0, 0, 1));
          }
        }
      }
    }

    // genarate knight moves
    if ((engine->board.side == white) ? piece == N : piece == n) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks =
            knight_attacks[source_square] &
            ((engine->board.side == white) ? ~engine->board.occupancies[white]
                                           : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (!get_bit(((engine->board.side == white)
                            ? engine->board.occupancies[black]
                            : engine->board.occupancies[white]),
                       target_square))
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 0, 0, 0, 0));

          else
            // capture move
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 1, 0, 0, 0));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate bishop moves
    if ((engine->board.side == white) ? piece == B : piece == b) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks =
            get_bishop_attacks(source_square,
                               engine->board.occupancies[both]) &
            ((engine->board.side == white) ? ~engine->board.occupancies[white]
                                           : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (!get_bit(((engine->board.side == white)
                            ? engine->board.occupancies[black]
                            : engine->board.occupancies[white]),
                       target_square))
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 0, 0, 0, 0));

          else
            // capture move
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 1, 0, 0, 0));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate rook moves
    if ((engine->board.side == white) ? piece == R : piece == r) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks =
            get_rook_attacks(source_square,
                             engine->board.occupancies[both]) &
            ((engine->board.side == white) ? ~engine->board.occupancies[white]
                                           : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (!get_bit(((engine->board.side == white)
                            ? engine->board.occupancies[black]
                            : engine->board.occupancies[white]),
                       target_square))
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 0, 0, 0, 0));

          else
            // capture move
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 1, 0, 0, 0));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate queen moves
    if ((engine->board.side == white) ? piece == Q : piece == q) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks =
            get_queen_attacks(source_square,
                              engine->board.occupancies[both]) &
            ((engine->board.side == white) ? ~engine->board.occupancies[white]
                                           : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (!get_bit(((engine->board.side == white)
                            ? engine->board.occupancies[black]
                            : engine->board.occupancies[white]),
                       target_square))
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 0, 0, 0, 0));

          else
            // capture move
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 1, 0, 0, 0));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate king moves
    if ((engine->board.side == white) ? piece == K : piece == k) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks =
            king_attacks[source_square] &
            ((engine->board.side == white) ? ~engine->board.occupancies[white]
                                           : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (!get_bit(((engine->board.side == white)
                            ? engine->board.occupancies[black]
                            : engine->board.occupancies[white]),
                       target_square))
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 0, 0, 0, 0));

          else
            // capture move
            add_move(move_list, encode_move(source_square, target_square, piece,
                                            0, 1, 0, 0, 0));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }
  }
}
