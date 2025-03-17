#include "movegen.h"
#include "attacks.h"
#include "bitboards.h"
#include "enums.h"
#include "move.h"
#include "structs.h"
#include <stdio.h>
#include <string.h>

extern nnue_settings_t nnue_settings;
extern keys_t keys;

const uint8_t castling_rights[64] = {
    7,  15, 15, 15, 3,  15, 15, 11, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 13, 15, 15, 15, 12, 15, 15, 14};

uint8_t make_move(position_t *pos, uint16_t move) {
  // preserve board state
  position_t pos_copy = *pos;

  // parse move
  uint8_t capture = get_move_capture(move);
  uint8_t source_square = get_move_source(move);
  uint8_t target_square = get_move_target(move);
  uint8_t piece = pos->mailbox[get_move_source(move)];
  uint8_t promoted_piece = get_move_promoted(pos->side, move);
  uint8_t double_push = get_move_double(move);
  uint8_t enpass = get_move_enpassant(move);
  uint8_t castling = get_move_castling(move);

  // increment fifty move rule counter
  pos->fifty++;

  // if pawn moved
  if (piece == P || piece == p)
    // reset fifty move rule counter
    pos->fifty = 0;

  // handling capture moves
  if (capture) {
    // reset fifty move rule counter
    pos->fifty = 0;

    // loop over bitboards opposite to the current side to move
    // if there's a piece on the target square
    uint8_t bb_piece = pos->mailbox[target_square];
    if (bb_piece != NO_PIECE &&
        get_bit(pos->bitboards[bb_piece], target_square)) {

      // remove it from corresponding bitboard
      pop_bit(pos->bitboards[bb_piece], target_square);

      // remove the piece from hash key
      pos->hash_keys.hash_key ^= keys.piece_keys[bb_piece][target_square];
      if (bb_piece == p || bb_piece == P) {
        pos->hash_keys.pawn_key ^= keys.piece_keys[bb_piece][target_square];
      } else {
        if (pos->side == white) {
          pos->hash_keys.non_pawn_key[black] ^=
              keys.piece_keys[bb_piece][target_square];
        } else {
          pos->hash_keys.non_pawn_key[white] ^=
              keys.piece_keys[bb_piece][target_square];
        }
      }
    }
  }

  // handle enpassant captures
  if (enpass) {
    // white to move
    if (pos->side == white) {
      // remove captured pawn
      pop_bit(pos->bitboards[p], target_square + 8);
      pos->mailbox[target_square + 8] = NO_PIECE;

      // remove pawn from hash key
      pos->hash_keys.hash_key ^= keys.piece_keys[p][target_square + 8];
      pos->hash_keys.pawn_key ^= keys.piece_keys[p][target_square + 8];
    }

    // black to move
    else {
      // remove captured pawn
      pop_bit(pos->bitboards[P], target_square - 8);
      pos->mailbox[target_square - 8] = NO_PIECE;

      // remove pawn from hash key
      pos->hash_keys.hash_key ^= keys.piece_keys[P][target_square - 8];
      pos->hash_keys.pawn_key ^= keys.piece_keys[P][target_square - 8];
    }
  }

  // move piece
  pop_bit(pos->bitboards[piece], source_square);
  set_bit(pos->bitboards[piece], target_square);
  pos->mailbox[source_square] = NO_PIECE;
  pos->mailbox[target_square] = piece;

  // hash piece
  pos->hash_keys.hash_key ^=
      keys.piece_keys[piece][source_square]; // remove piece from source
                                             // square in hash key
  pos->hash_keys.hash_key ^=
      keys.piece_keys[piece][target_square]; // set piece to the target
                                             // square in hash key
  if (piece == p || piece == P) {
    pos->hash_keys.pawn_key ^= keys.piece_keys[piece][source_square];
    pos->hash_keys.pawn_key ^= keys.piece_keys[piece][target_square];
  } else {
    if (pos->side == white) {
      pos->hash_keys.non_pawn_key[white] ^=
          keys.piece_keys[piece][source_square];
      pos->hash_keys.non_pawn_key[white] ^=
          keys.piece_keys[piece][target_square];
    } else {
      pos->hash_keys.non_pawn_key[black] ^=
          keys.piece_keys[piece][source_square];
      pos->hash_keys.non_pawn_key[black] ^=
          keys.piece_keys[piece][target_square];
    }
  }

  // handle pawn promotions
  if (promoted_piece) {
    // white to move
    if (pos->side == white) {
      // erase the pawn from the target square
      pop_bit(pos->bitboards[P], target_square);

      // remove pawn from hash key
      pos->hash_keys.hash_key ^= keys.piece_keys[P][target_square];
      pos->hash_keys.pawn_key ^= keys.piece_keys[P][target_square];
    }

    // black to move
    else {
      // erase the pawn from the target square
      pop_bit(pos->bitboards[p], target_square);

      // remove pawn from hash key
      pos->hash_keys.hash_key ^= keys.piece_keys[p][target_square];
      pos->hash_keys.pawn_key ^= keys.piece_keys[p][target_square];
    }

    // set up promoted piece on chess board
    set_bit(pos->bitboards[promoted_piece], target_square);
    pos->mailbox[target_square] = promoted_piece;

    // add promoted piece into the hash key
    pos->hash_keys.hash_key ^= keys.piece_keys[promoted_piece][target_square];
    if (pos->side == white) {
      pos->hash_keys.non_pawn_key[white] ^=
          keys.piece_keys[promoted_piece][target_square];
    } else {
      pos->hash_keys.non_pawn_key[black] ^=
          keys.piece_keys[promoted_piece][target_square];
    }
  }

  // hash enpassant if available (remove enpassant square from hash key)
  if (pos->enpassant != no_sq) {
    pos->hash_keys.hash_key ^= keys.enpassant_keys[pos->enpassant];
  }

  // reset enpassant square
  pos->enpassant = no_sq;

  // handle double pawn push
  if (double_push) {
    // white to move
    if (pos->side == white) {
      // set enpassant square
      pos->enpassant = target_square + 8;

      // hash enpassant
      pos->hash_keys.hash_key ^= keys.enpassant_keys[target_square + 8];
    }

    // black to move
    else {
      // set enpassant square
      pos->enpassant = target_square - 8;

      // hash enpassant
      pos->hash_keys.hash_key ^= keys.enpassant_keys[target_square - 8];
    }
  }

  // handle castling moves
  if (castling) {
    // Lookup tables for rook movement during castling
    static const int rook_start[4] = {h1, a1, h8,
                                      a8}; // H-file and A-file rooks
    static const int rook_end[4] = {f1, d1, f8,
                                    d8}; // Destination squares for rooks
    static const int castle_squares[4] = {
        g1, c1, g8, c8}; // Target squares for castling moves
    static const int rook_piece[4] = {R, R, r, r}; // Corresponding rook pieces

    for (int i = 0; i < 4; ++i) {
      if (target_square == castle_squares[i]) {
        int r_start = rook_start[i];
        int r_end = rook_end[i];
        int piece = rook_piece[i];

        // Move rook
        pop_bit(pos->bitboards[piece], r_start);
        set_bit(pos->bitboards[piece], r_end);
        pos->mailbox[r_start] = NO_PIECE;
        pos->mailbox[r_end] = piece;

        // Update hash key
        pos->hash_keys.hash_key ^= keys.piece_keys[piece][r_start];
        pos->hash_keys.hash_key ^= keys.piece_keys[piece][r_end];
        if (pos->side == white) {
          pos->hash_keys.non_pawn_key[white] ^= keys.piece_keys[piece][r_start];
          pos->hash_keys.non_pawn_key[white] ^= keys.piece_keys[piece][r_end];
        } else {
          pos->hash_keys.non_pawn_key[black] ^= keys.piece_keys[piece][r_start];
          pos->hash_keys.non_pawn_key[black] ^= keys.piece_keys[piece][r_end];
        }
        break;
      }
    }
  }

  // hash castling
  pos->hash_keys.hash_key ^= keys.castle_keys[pos->castle];

  // update castling rights
  pos->castle &= castling_rights[source_square];
  pos->castle &= castling_rights[target_square];

  // hash castling
  pos->hash_keys.hash_key ^= keys.castle_keys[pos->castle];

  // reset occupancies
  memset(pos->occupancies, 0ULL, 24);

  // loop over white pieces bitboards
  for (int bb_piece = P; bb_piece <= K; bb_piece++)
    // update white occupancies
    pos->occupancies[white] |= pos->bitboards[bb_piece];

  // loop over black pieces bitboards
  for (int bb_piece = p; bb_piece <= k; bb_piece++)
    // update black occupancies
    pos->occupancies[black] |= pos->bitboards[bb_piece];

  // update both sides occupancies
  pos->occupancies[both] |= pos->occupancies[white];
  pos->occupancies[both] |= pos->occupancies[black];

  // change side
  pos->side ^= 1;

  // hash side
  pos->hash_keys.hash_key ^= keys.side_key;

  // make sure that king has not been exposed into a check
  if (is_square_attacked(pos,
                         (pos->side == white)
                             ? __builtin_ctzll(pos->bitboards[k])
                             : __builtin_ctzll(pos->bitboards[K]),
                         pos->side)) {
    // take move back
    *pos = pos_copy;

    // return illegal move
    return 0;
  }

  // otherwise
  else
    // return legal move
    return 1;
}

// add move to the move list
void add_move(moves *move_list, int move) {
  // store move
  move_list->entry[move_list->count].move = move;

  // increment move count
  move_list->count++;
}

// generate all moves
void generate_moves(position_t *pos, moves *move_list) {
  // init move count
  move_list->count = 0;

  // define source & target squares
  int source_square, target_square;

  // define current piece's bitboard copy & its attacks
  uint64_t bitboard, attacks;

  // loop over all the bitboards
  for (uint8_t piece = P; piece <= k; piece++) {
    // init piece bitboard copy
    bitboard = pos->bitboards[piece];

    // generate white pawns & white king castling moves
    if (pos->side == white) {
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
              !get_bit(pos->occupancies[both], target_square)) {
            // pawn promotion
            if (source_square >= a7 && source_square <= h7) {
              add_move(move_list, encode_move(source_square, target_square,
                                              QUEEN_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              ROOK_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              BISHOP_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              KNIGHT_PROMOTION));
            }

            else {
              // one square ahead pawn move
              add_move(move_list,
                       encode_move(source_square, target_square, QUIET));

              // two squares ahead pawn move
              if ((source_square >= a2 && source_square <= h2) &&
                  !get_bit(pos->occupancies[both], target_square - 8))
                add_move(move_list,
                         encode_move(source_square, (target_square - 8),
                                     DOUBLE_PUSH));
            }
          }

          // init pawn attacks bitboard
          attacks =
              pawn_attacks[pos->side][source_square] & pos->occupancies[black];

          // generate pawn captures
          while (attacks) {
            // init target square
            target_square = __builtin_ctzll(attacks);

            // pawn promotion
            if (source_square >= a7 && source_square <= h7) {
              add_move(move_list, encode_move(source_square, target_square,
                                              QUEEN_CAPTURE_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              ROOK_CAPTURE_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              BISHOP_CAPTURE_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              KNIGHT_CAPTURE_PROMOTION));
            }

            else
              // one square ahead pawn move
              add_move(move_list,
                       encode_move(source_square, target_square, CAPTURE));

            // pop ls1b of the pawn attacks
            pop_bit(attacks, target_square);
          }

          // generate enpassant captures
          if (pos->enpassant != no_sq) {
            // lookup pawn attacks and bitwise AND with enpassant square (bit)
            uint64_t enpassant_attacks =
                pawn_attacks[pos->side][source_square] &
                (1ULL << pos->enpassant);

            // make sure enpassant capture available
            if (enpassant_attacks) {
              // init enpassant capture target square
              int target_enpassant = __builtin_ctzll(enpassant_attacks);
              add_move(move_list, encode_move(source_square, target_enpassant,
                                              ENPASSANT_CAPTURE));
            }
          }

          // pop ls1b from piece bitboard copy
          pop_bit(bitboard, source_square);
        }
      }

      // castling moves
      if (piece == K) {
        // king side castling is available
        if (pos->castle & wk) {
          // make sure square between king and king's rook are empty
          if (!get_bit(pos->occupancies[both], f1) &&
              !get_bit(pos->occupancies[both], g1)) {
            // make sure king and the f1 squares are not under attacks
            if (!is_square_attacked(pos, e1, black) &&
                !is_square_attacked(pos, f1, black))
              add_move(move_list, encode_move(e1, g1, KING_CASTLE));
          }
        }

        // queen side castling is available
        if (pos->castle & wq) {
          // make sure square between king and queen's rook are empty
          if (!get_bit(pos->occupancies[both], d1) &&
              !get_bit(pos->occupancies[both], c1) &&
              !get_bit(pos->occupancies[both], b1)) {
            // make sure king and the d1 squares are not under attacks
            if (!is_square_attacked(pos, e1, black) &&
                !is_square_attacked(pos, d1, black))
              add_move(move_list, encode_move(e1, c1, QUEEN_CASTLE));
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
              !get_bit(pos->occupancies[both], target_square)) {
            // pawn promotion
            if (source_square >= a2 && source_square <= h2) {
              add_move(move_list, encode_move(source_square, target_square,
                                              QUEEN_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              ROOK_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              BISHOP_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              KNIGHT_PROMOTION));
            }

            else {
              // one square ahead pawn move
              add_move(move_list,
                       encode_move(source_square, target_square, QUIET));

              // two squares ahead pawn move
              if ((source_square >= a7 && source_square <= h7) &&
                  !get_bit(pos->occupancies[both], target_square + 8))
                add_move(move_list,
                         encode_move(source_square, (target_square + 8),
                                     DOUBLE_PUSH));
            }
          }

          // init pawn attacks bitboard
          attacks =
              pawn_attacks[pos->side][source_square] & pos->occupancies[white];

          // generate pawn captures
          while (attacks) {
            // init target square
            target_square = __builtin_ctzll(attacks);

            // pawn promotion
            if (source_square >= a2 && source_square <= h2) {
              add_move(move_list, encode_move(source_square, target_square,
                                              QUEEN_CAPTURE_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              ROOK_CAPTURE_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              BISHOP_CAPTURE_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              KNIGHT_CAPTURE_PROMOTION));
            }

            else
              // one square ahead pawn move
              add_move(move_list,
                       encode_move(source_square, target_square, CAPTURE));

            // pop ls1b of the pawn attacks
            pop_bit(attacks, target_square);
          }

          // generate enpassant captures
          if (pos->enpassant != no_sq) {
            // lookup pawn attacks and bitwise AND with enpassant square (bit)
            uint64_t enpassant_attacks =
                pawn_attacks[pos->side][source_square] &
                (1ULL << pos->enpassant);

            // make sure enpassant capture available
            if (enpassant_attacks) {
              // init enpassant capture target square
              int target_enpassant = __builtin_ctzll(enpassant_attacks);
              add_move(move_list, encode_move(source_square, target_enpassant,
                                              ENPASSANT_CAPTURE));
            }
          }

          // pop ls1b from piece bitboard copy
          pop_bit(bitboard, source_square);
        }
      }

      // castling moves
      if (piece == k) {
        // king side castling is available
        if (pos->castle & bk) {
          // make sure square between king and king's rook are empty
          if (!get_bit(pos->occupancies[both], f8) &&
              !get_bit(pos->occupancies[both], g8)) {
            // make sure king and the f8 squares are not under attacks
            if (!is_square_attacked(pos, e8, white) &&
                !is_square_attacked(pos, f8, white))
              add_move(move_list, encode_move(e8, g8, KING_CASTLE));
          }
        }

        // queen side castling is available
        if (pos->castle & bq) {
          // make sure square between king and queen's rook are empty
          if (!get_bit(pos->occupancies[both], d8) &&
              !get_bit(pos->occupancies[both], c8) &&
              !get_bit(pos->occupancies[both], b8)) {
            // make sure king and the d8 squares are not under attacks
            if (!is_square_attacked(pos, e8, white) &&
                !is_square_attacked(pos, d8, white))
              add_move(move_list, encode_move(e8, c8, QUEEN_CASTLE));
          }
        }
      }
    }

    // genarate knight moves
    if ((pos->side == white) ? piece == N : piece == n) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = knight_attacks[source_square] &
                  ((pos->side == white) ? ~pos->occupancies[white]
                                        : ~pos->occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (!get_bit(((pos->side == white) ? pos->occupancies[black]
                                             : pos->occupancies[white]),
                       target_square))
            add_move(move_list,
                     encode_move(source_square, target_square, QUIET));

          else
            // capture move
            add_move(move_list,
                     encode_move(source_square, target_square, CAPTURE));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate bishop moves
    if ((pos->side == white) ? piece == B : piece == b) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = get_bishop_attacks(source_square, pos->occupancies[both]) &
                  ((pos->side == white) ? ~pos->occupancies[white]
                                        : ~pos->occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (!get_bit(((pos->side == white) ? pos->occupancies[black]
                                             : pos->occupancies[white]),
                       target_square))
            add_move(move_list,
                     encode_move(source_square, target_square, QUIET));

          else
            // capture move
            add_move(move_list,
                     encode_move(source_square, target_square, CAPTURE));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate rook moves
    if ((pos->side == white) ? piece == R : piece == r) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = get_rook_attacks(source_square, pos->occupancies[both]) &
                  ((pos->side == white) ? ~pos->occupancies[white]
                                        : ~pos->occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (!get_bit(((pos->side == white) ? pos->occupancies[black]
                                             : pos->occupancies[white]),
                       target_square))
            add_move(move_list,
                     encode_move(source_square, target_square, QUIET));

          else
            // capture move
            add_move(move_list,
                     encode_move(source_square, target_square, CAPTURE));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate queen moves
    if ((pos->side == white) ? piece == Q : piece == q) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = get_queen_attacks(source_square, pos->occupancies[both]) &
                  ((pos->side == white) ? ~pos->occupancies[white]
                                        : ~pos->occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (!get_bit(((pos->side == white) ? pos->occupancies[black]
                                             : pos->occupancies[white]),
                       target_square))
            add_move(move_list,
                     encode_move(source_square, target_square, QUIET));

          else
            // capture move
            add_move(move_list,
                     encode_move(source_square, target_square, CAPTURE));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate king moves
    if ((pos->side == white) ? piece == K : piece == k) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = king_attacks[source_square] &
                  ((pos->side == white) ? ~pos->occupancies[white]
                                        : ~pos->occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          // quiet move
          if (!get_bit(((pos->side == white) ? pos->occupancies[black]
                                             : pos->occupancies[white]),
                       target_square))
            add_move(move_list,
                     encode_move(source_square, target_square, QUIET));

          else
            // capture move
            add_move(move_list,
                     encode_move(source_square, target_square, CAPTURE));

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }
  }
}

void generate_noisy(position_t *pos, moves *move_list) {
  // init move count
  move_list->count = 0;

  // define source & target squares
  int source_square, target_square;

  // define current piece's bitboard copy & its attacks
  uint64_t bitboard, attacks;

  // loop over all the bitboards
  for (uint8_t piece = P; piece <= k; piece++) {
    // init piece bitboard copy
    bitboard = pos->bitboards[piece];

    // generate white pawns & white king castling moves
    if (pos->side == white) {
      // pick up white pawn bitboards index
      if (piece == P) {
        // loop over white pawns within white pawn bitboard
        while (bitboard) {
          // init source square
          source_square = __builtin_ctzll(bitboard);

          // init target square
          target_square = source_square - 8;

          // generate pawn promotions
          if (!(target_square < a8) &&
              !get_bit(pos->occupancies[both], target_square)) {
            // pawn promotion
            if (source_square >= a7 && source_square <= h7) {
              add_move(move_list, encode_move(source_square, target_square,
                                              QUEEN_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              ROOK_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              BISHOP_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              KNIGHT_PROMOTION));
            }
          }

          // init pawn attacks bitboard
          attacks =
              pawn_attacks[pos->side][source_square] & pos->occupancies[black];

          // generate pawn captures
          while (attacks) {
            // init target square
            target_square = __builtin_ctzll(attacks);

            // pawn capture promotion
            if (source_square >= a7 && source_square <= h7) {
              add_move(move_list, encode_move(source_square, target_square,
                                              QUEEN_CAPTURE_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              ROOK_CAPTURE_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              BISHOP_CAPTURE_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              KNIGHT_CAPTURE_PROMOTION));
            }

            else
              // one square ahead pawn move
              add_move(move_list,
                       encode_move(source_square, target_square, CAPTURE));

            // pop ls1b of the pawn attacks
            pop_bit(attacks, target_square);
          }

          // generate enpassant captures
          if (pos->enpassant != no_sq) {
            // lookup pawn attacks and bitwise AND with enpassant square (bit)
            uint64_t enpassant_attacks =
                pawn_attacks[pos->side][source_square] &
                (1ULL << pos->enpassant);

            // make sure enpassant capture available
            if (enpassant_attacks) {
              // init enpassant capture target square
              int target_enpassant = __builtin_ctzll(enpassant_attacks);
              add_move(move_list, encode_move(source_square, target_enpassant,
                                              ENPASSANT_CAPTURE));
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

          // generate pawn promotions
          if (!(target_square > h1) &&
              !get_bit(pos->occupancies[both], target_square)) {
            // pawn promotion
            if (source_square >= a2 && source_square <= h2) {
              add_move(move_list, encode_move(source_square, target_square,
                                              QUEEN_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              ROOK_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              BISHOP_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              KNIGHT_PROMOTION));
            }
          }

          // init pawn attacks bitboard
          attacks =
              pawn_attacks[pos->side][source_square] & pos->occupancies[white];

          // generate pawn captures
          while (attacks) {
            // init target square
            target_square = __builtin_ctzll(attacks);

            // pawn capture promotion
            if (source_square >= a2 && source_square <= h2) {
              add_move(move_list, encode_move(source_square, target_square,
                                              QUEEN_CAPTURE_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              ROOK_CAPTURE_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              BISHOP_CAPTURE_PROMOTION));
              add_move(move_list, encode_move(source_square, target_square,
                                              KNIGHT_CAPTURE_PROMOTION));
            }

            else
              // one square ahead pawn move
              add_move(move_list,
                       encode_move(source_square, target_square, CAPTURE));

            // pop ls1b of the pawn attacks
            pop_bit(attacks, target_square);
          }

          // generate enpassant captures
          if (pos->enpassant != no_sq) {
            // lookup pawn attacks and bitwise AND with enpassant square (bit)
            uint64_t enpassant_attacks =
                pawn_attacks[pos->side][source_square] &
                (1ULL << pos->enpassant);

            // make sure enpassant capture available
            if (enpassant_attacks) {
              // init enpassant capture target square
              int target_enpassant = __builtin_ctzll(enpassant_attacks);
              add_move(move_list, encode_move(source_square, target_enpassant,
                                              ENPASSANT_CAPTURE));
            }
          }

          // pop ls1b from piece bitboard copy
          pop_bit(bitboard, source_square);
        }
      }
    }

    // genarate knight moves
    if ((pos->side == white) ? piece == N : piece == n) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = knight_attacks[source_square] &
                  ((pos->side == white) ? ~pos->occupancies[white]
                                        : ~pos->occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          if (get_bit(((pos->side == white) ? pos->occupancies[black]
                                            : pos->occupancies[white]),
                      target_square)) {
            add_move(move_list,
                     encode_move(source_square, target_square, CAPTURE));
          }

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate bishop moves
    if ((pos->side == white) ? piece == B : piece == b) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = get_bishop_attacks(source_square, pos->occupancies[both]) &
                  ((pos->side == white) ? ~pos->occupancies[white]
                                        : ~pos->occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          if (get_bit(((pos->side == white) ? pos->occupancies[black]
                                            : pos->occupancies[white]),
                      target_square)) {
            add_move(move_list,
                     encode_move(source_square, target_square, CAPTURE));
          }
          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate rook moves
    if ((pos->side == white) ? piece == R : piece == r) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = get_rook_attacks(source_square, pos->occupancies[both]) &
                  ((pos->side == white) ? ~pos->occupancies[white]
                                        : ~pos->occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          if (get_bit(((pos->side == white) ? pos->occupancies[black]
                                            : pos->occupancies[white]),
                      target_square)) {
            add_move(move_list,
                     encode_move(source_square, target_square, CAPTURE));
          }

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate queen moves
    if ((pos->side == white) ? piece == Q : piece == q) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = get_queen_attacks(source_square, pos->occupancies[both]) &
                  ((pos->side == white) ? ~pos->occupancies[white]
                                        : ~pos->occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          if (get_bit(((pos->side == white) ? pos->occupancies[black]
                                            : pos->occupancies[white]),
                      target_square)) {
            add_move(move_list,
                     encode_move(source_square, target_square, CAPTURE));
          }

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }

    // generate king moves
    if ((pos->side == white) ? piece == K : piece == k) {
      // loop over source squares of piece bitboard copy
      while (bitboard) {
        // init source square
        source_square = __builtin_ctzll(bitboard);

        // init piece attacks in order to get set of target squares
        attacks = king_attacks[source_square] &
                  ((pos->side == white) ? ~pos->occupancies[white]
                                        : ~pos->occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = __builtin_ctzll(attacks);

          if (get_bit(((pos->side == white) ? pos->occupancies[black]
                                            : pos->occupancies[white]),
                      target_square)) {
            add_move(move_list,
                     encode_move(source_square, target_square, CAPTURE));
          }

          // pop ls1b in current attacks set
          pop_bit(attacks, target_square);
        }

        // pop ls1b of the current piece bitboard copy
        pop_bit(bitboard, source_square);
      }
    }
  }
}
