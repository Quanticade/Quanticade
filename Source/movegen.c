#include "movegen.h"
#include "attacks.h"
#include "bitboards.h"
#include "enums.h"
#include "move.h"
#include "structs.h"
#include "uci.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern nnue_settings_t nnue_settings;
extern keys_t keys;

uint64_t between[64][64] = {0};
uint64_t line[64][64] = {0};

#define RANK7_MASK 0x000000000000FF00ULL
#define RANK2_MASK 0x00FF000000000000ULL

void init_between_bitboards(void) {
  for (int from = 0; from < 64; ++from) {
    for (int to = 0; to < 64; ++to) {
      if (from == to) {
        between[from][to] = 0ULL;
        line[from][to] = 0ULL;
        continue;
      }
      line[from][to] = 0ULL;
      if (get_bishop_attacks(from, BB(0)) & BB(to))
        line[from][to] |=
            get_bishop_attacks(from, BB(0)) & get_bishop_attacks(to, BB(0));
      if (get_rook_attacks(from, BB(0)) & BB(to))
        line[from][to] |=
            get_rook_attacks(from, BB(0)) & get_rook_attacks(to, BB(0));
      line[from][to] |= BB(from) | BB(to);

      between[from][to] = 0ULL;
      between[from][to] |=
          get_bishop_attacks(from, BB(to)) & get_bishop_attacks(to, BB(from));
      between[from][to] |=
          get_rook_attacks(from, BB(to)) & get_rook_attacks(to, BB(from));
      between[from][to] |= BB(to);
      between[from][to] &= line[from][to];
    }
  }
}

void update_slider_pins(position_t *pos, uint8_t side) {
  int king = get_lsb(pos->bitboards[KING + 6 * side]);
  pos->blockers[side] = 0;

  uint64_t possible_bishop_pinners = get_bishop_attacks(king, BB(0)) &
                                     (pos->bitboards[BISHOP + 6 * (side ^ 1)] |
                                      pos->bitboards[QUEEN + 6 * (side ^ 1)]);
  uint64_t possible_rook_pinners =
      get_rook_attacks(king, BB(0)) & (pos->bitboards[ROOK + 6 * (side ^ 1)] |
                                       pos->bitboards[QUEEN + 6 * (side ^ 1)]);
  uint64_t possible_pinners = possible_bishop_pinners | possible_rook_pinners;
  uint64_t occupied = (pos->occupancies[white] | pos->occupancies[black]) ^ possible_pinners;

  while (possible_pinners) {
    int pinner_square = poplsb(&possible_pinners);
    uint64_t pinned_bb = between[king][pinner_square] & occupied;
    if (popcount(pinned_bb) == 1)
      pos->blockers[side] |= pinned_bb;
  }
}

uint8_t is_pseudo_legal(position_t *pos, uint16_t move) {
  uint8_t origin    = get_move_source(move);
  uint8_t target    = get_move_target(move);
  uint8_t piece     = pos->mailbox[origin];
  uint8_t noc_piece = piece % 6;

  if (piece == NO_PIECE || pos->side != piece / 6)
    return 0;

  if (get_move_capture(move)) {
    if (move & 2 && !is_move_promotion(move))
      return 0;
    if (!get_move_enpassant(move)) {
      uint8_t opp = pos->mailbox[target];
      if (opp == NO_PIECE || pos->side == opp / 6)
        return 0;
    }
  } else if (!get_move_castling(move)) {
    if (pos->mailbox[target] != NO_PIECE)
      return 0;
  }

  uint8_t castling_type = get_move_castling(move);

  if (castling_type) {
    if (noc_piece != KING || pos->castle == 0)
      return 0;

    uint8_t cs = castle_side(castling_type);
    if (!(pos->castle & castle_bit(pos->side, cs)))
      return 0;

    uint8_t ksq = origin;
    uint8_t rsq = target;
    if (pos->castle_rook_sq[pos->side][cs] != rsq)
      return 0;
    if (pos->mailbox[rsq] != ROOK + 6 * pos->side ||
        pos->mailbox[ksq] != KING + 6 * pos->side)
      return 0;

    uint8_t kdest = castle_king_dest(pos->side, cs);
    uint8_t rdest = castle_rook_dest(pos->side, cs);

    uint64_t must_empty = ((between[ksq][kdest] | BB(kdest)) |
                           (between[rsq][rdest] | BB(rdest))) &
                          ~(BB(ksq) | BB(rsq));
    return !(must_empty & (pos->occupancies[white] | pos->occupancies[black]));

  } else if (get_move_enpassant(move)) {
    if (noc_piece != PAWN)
      return 0;

    int captured_square = target + (pos->side ? -8 : 8);

    if (target != pos->enpassant)
      return 0;

    if (origin / 8 != (pos->side ? 4 : 3))
      return 0;

    if (abs((origin % 8) - (target % 8)) != 1)
      return 0;

    int cap = pos->mailbox[captured_square];
    if (cap % 6 != PAWN || cap / 6 == pos->side)
      return 0;

    return 1;

  } else if (is_move_promotion(move)) {
    if (noc_piece != PAWN)
      return 0;
    if (!(BB(target) & 0xFF000000000000FF))
      return 0;
    if (!(get_pawn_attacks(pos->side, origin) & BB(target) &
          pos->occupancies[pos->side ^ 1]) &&
        !(origin + (pos->side ? 8 : -8) == target &&
          pos->mailbox[target] == NO_PIECE))
      return 0;
    return 1;
  }

  if ((get_move_enpassant(move) || get_move_double(move)) && noc_piece != PAWN)
    return 0;

  switch (noc_piece) {
  case PAWN:
    if (BB(target) & 0xFF000000000000FF)
      return 0;
    if (!(get_pawn_attacks(pos->side, origin) & BB(target) &&
          get_move_capture(move)) &&
        !(origin + (pos->side ? 8 : -8) == target && !get_move_double(move) &&
          pos->mailbox[target] == NO_PIECE) &&
        !(origin + 2 * (pos->side ? 8 : -8) == target &&
          get_move_double(move) && BB(origin) & 0x00FF00000000FF00 &&
          pos->mailbox[target] == NO_PIECE &&
          pos->mailbox[target - (pos->side ? 8 : -8)] == NO_PIECE &&
          BB(target) & 0xFFFF000000))
      return 0;
    break;
  case KNIGHT:
    if (!(knight_attacks[origin] & BB(target))) return 0;
    break;
  case BISHOP:
    if (!(get_bishop_attacks(origin, pos->occupancies[white] | pos->occupancies[black]) & BB(target))) return 0;
    break;
  case ROOK:
    if (!(get_rook_attacks(origin, pos->occupancies[white] | pos->occupancies[black]) & BB(target))) return 0;
    break;
  case QUEEN:
    if (!(get_queen_attacks(origin, pos->occupancies[white] | pos->occupancies[black]) & BB(target))) return 0;
    break;
  case KING:
    if (!(king_attacks[origin] & BB(target))) return 0;
    break;
  default:
    break;
  }

  return 1;
}

uint8_t is_legal(position_t *pos, uint16_t move) {
  uint8_t  source    = get_move_source(move);
  uint8_t  target    = get_move_target(move);
  uint64_t source_bb = BB(source);
  uint8_t  stm       = pos->side;
  uint8_t  king      = get_lsb(pos->bitboards[KING + 6 * stm]);

  if (pos->checkers && source != king) {
    if (pos->checker_count > 1)
      return 0;
    uint8_t attacker   = get_lsb(pos->checkers);
    uint8_t mod_target = get_move_enpassant(move) ? target - (stm ? 8 : -8) : target;
    if (between[king][attacker] & BB(target) || mod_target == attacker)
      goto pinners;
    return 0;
  }

  if (get_move_enpassant(move)) {
    uint8_t  ep_square = target - (stm ? 8 : -8);
    uint64_t occupied  = ((pos->occupancies[white] | pos->occupancies[black]) ^ source_bb ^ BB(ep_square)) | BB(target);

    uint64_t rook_attacks = get_rook_attacks(king, occupied) &
                            (pos->bitboards[ROOK  + 6 * (stm ^ 1)] |
                             pos->bitboards[QUEEN + 6 * (stm ^ 1)]);
    uint64_t bishop_attacks = get_bishop_attacks(king, occupied) &
                              (pos->bitboards[BISHOP + 6 * (stm ^ 1)] |
                               pos->bitboards[QUEEN  + 6 * (stm ^ 1)]);
    return !rook_attacks && !bishop_attacks;
  }

  if (get_move_castling(move)) {
    if (pos->blockers[stm] & BB(target))
      return 0;
    uint8_t  cs      = castle_side(get_move_castling(move));
    uint8_t  kdest   = castle_king_dest(stm, cs);
    uint64_t squares = between[source][kdest] | source_bb;
    while (squares) {
      uint8_t sq = poplsb(&squares);
      if (attackers_to(pos, sq, pos->occupancies[white] | pos->occupancies[black]) & pos->occupancies[stm ^ 1])
        return 0;
    }
    return 1;
  }

  if (pos->mailbox[source] == KING + 6 * stm) {
    uint64_t occupied = (pos->occupancies[white] | pos->occupancies[black]) ^ BB(source);
    return !(pos->occupancies[stm ^ 1] & attackers_to(pos, target, occupied));
  }

pinners:;
  uint64_t pinned = pos->blockers[stm] & source_bb;
  return !pinned || (line[source][target] & BB(king));
}

void make_move(position_t *pos, uint16_t move) {
  // parse move
  uint8_t capture        = get_move_capture(move);
  uint8_t source_square  = get_move_source(move);
  uint8_t target_square  = get_move_target(move);
  uint8_t piece          = pos->mailbox[source_square];
  uint8_t promoted_piece = get_move_promoted(pos->side, move);
  uint8_t double_push    = get_move_double(move);
  uint8_t enpass         = get_move_enpassant(move);
  uint8_t castling       = get_move_castling(move);
  uint8_t stm            = pos->side;

  // increment fifty move rule counter
  pos->fifty++;
  // if pawn moved, reset fifty move rule counter
  if (piece == P || piece == p)
    pos->fifty = 0;

  // handling capture moves
  if (capture && !enpass) {
    pos->fifty = 0;
    uint8_t bb_piece = pos->mailbox[target_square];
    if (bb_piece != NO_PIECE && get_bit(pos->bitboards[bb_piece], target_square)) {
      pop_bit(pos->bitboards[bb_piece], target_square);
      pos->hash_keys.hash_key ^= keys.piece_keys[bb_piece][target_square];
      if (bb_piece == p || bb_piece == P)
        pos->hash_keys.pawn_key ^= keys.piece_keys[bb_piece][target_square];
      else
        pos->hash_keys.non_pawn_key[stm ^ 1] ^= keys.piece_keys[bb_piece][target_square];
      pop_bit(pos->occupancies[stm ^ 1], target_square);
    }
  }

  // move piece
  if (!castling) {
    pop_bit(pos->bitboards[piece], source_square);
    set_bit(pos->bitboards[piece], target_square);
    pos->mailbox[source_square] = NO_PIECE;
    pos->mailbox[target_square] = piece;

    pop_bit(pos->occupancies[stm], source_square);
    set_bit(pos->occupancies[stm], target_square);

    // hash piece
    pos->hash_keys.hash_key ^= keys.piece_keys[piece][source_square];
    pos->hash_keys.hash_key ^= keys.piece_keys[piece][target_square];
    if (piece == p || piece == P) {
      pos->hash_keys.pawn_key ^= keys.piece_keys[piece][source_square];
      pos->hash_keys.pawn_key ^= keys.piece_keys[piece][target_square];
    } else {
      pos->hash_keys.non_pawn_key[stm] ^= keys.piece_keys[piece][source_square];
      pos->hash_keys.non_pawn_key[stm] ^= keys.piece_keys[piece][target_square];
    }
  }

  // handle enpassant captures
  if (enpass) {
    pos->fifty = 0;
    uint8_t ep_square = stm == white ? target_square + 8 : target_square - 8;
    uint8_t ep_pawn   = stm == white ? p : P;
    pop_bit(pos->bitboards[ep_pawn], ep_square);
    pos->mailbox[ep_square] = NO_PIECE;
    pos->hash_keys.hash_key ^= keys.piece_keys[ep_pawn][ep_square];
    pos->hash_keys.pawn_key ^= keys.piece_keys[ep_pawn][ep_square];
    pop_bit(pos->occupancies[stm ^ 1], ep_square);
  }

  // handle pawn promotions
  if (promoted_piece) {
    uint8_t pawn = stm == white ? P : p;
    pop_bit(pos->bitboards[pawn], target_square);
    pos->hash_keys.hash_key ^= keys.piece_keys[pawn][target_square];
    pos->hash_keys.pawn_key ^= keys.piece_keys[pawn][target_square];
    set_bit(pos->bitboards[promoted_piece], target_square);
    pos->mailbox[target_square] = promoted_piece;
    pos->hash_keys.hash_key         ^= keys.piece_keys[promoted_piece][target_square];
    pos->hash_keys.non_pawn_key[stm] ^= keys.piece_keys[promoted_piece][target_square];
  }

  // hash enpassant if available
  if (pos->enpassant != no_sq)
    pos->hash_keys.hash_key ^= keys.enpassant_keys[pos->enpassant];
  pos->enpassant = no_sq;

  // handle double pawn push
  if (double_push) {
    pos->enpassant = stm == white ? target_square + 8 : target_square - 8;
    pos->hash_keys.hash_key ^= keys.enpassant_keys[pos->enpassant];
  }

  if (castling) {
    uint8_t cs         = castle_side(castling);
    uint8_t king_piece = KING + 6 * stm;
    uint8_t rook_piece = ROOK + 6 * stm;
    uint8_t ksq        = source_square;
    uint8_t rsq        = target_square;
    uint8_t kdest      = castle_king_dest(stm, cs);
    uint8_t rdest      = castle_rook_dest(stm, cs);

    pop_bit(pos->bitboards[king_piece], ksq);
    pop_bit(pos->occupancies[stm], ksq);
    pos->mailbox[ksq] = NO_PIECE;
    pop_bit(pos->bitboards[rook_piece], rsq);
    pop_bit(pos->occupancies[stm], rsq);
    pos->mailbox[rsq] = NO_PIECE;

    set_bit(pos->bitboards[king_piece], kdest);
    set_bit(pos->occupancies[stm], kdest);
    pos->mailbox[kdest] = king_piece;
    set_bit(pos->bitboards[rook_piece], rdest);
    set_bit(pos->occupancies[stm], rdest);
    pos->mailbox[rdest] = rook_piece;

    pos->hash_keys.hash_key          ^= keys.piece_keys[king_piece][ksq];
    pos->hash_keys.hash_key          ^= keys.piece_keys[king_piece][kdest];
    pos->hash_keys.hash_key          ^= keys.piece_keys[rook_piece][rsq];
    pos->hash_keys.hash_key          ^= keys.piece_keys[rook_piece][rdest];
    pos->hash_keys.non_pawn_key[stm] ^= keys.piece_keys[king_piece][ksq];
    pos->hash_keys.non_pawn_key[stm] ^= keys.piece_keys[king_piece][kdest];
    pos->hash_keys.non_pawn_key[stm] ^= keys.piece_keys[rook_piece][rsq];
    pos->hash_keys.non_pawn_key[stm] ^= keys.piece_keys[rook_piece][rdest];
  }

  pos->hash_keys.hash_key ^= keys.castle_keys[pos->castle];
  if (piece == K)
    pos->castle &= ~(wk | wq);
  else if (piece == k)
    pos->castle &= ~(bk | bq);
  for (int col = white; col <= black; ++col) {
    for (int cs = 0; cs < 2; ++cs) {
      uint8_t rsq = pos->castle_rook_sq[col][cs];
      if (rsq != no_sq && (source_square == rsq || target_square == rsq)) {
        pos->castle &= ~castle_bit(col, cs);
        pos->castle_rook_sq[col][cs] = no_sq;
      }
    }
  }
  pos->hash_keys.hash_key ^= keys.castle_keys[pos->castle];

  pos->checkers = attackers_to(pos,
                             stm == white ? get_lsb(pos->bitboards[k])
                                          : get_lsb(pos->bitboards[K]),
                             pos->occupancies[white] | pos->occupancies[black]) &
                pos->occupancies[stm];
  pos->checker_count = popcount(pos->checkers);

  update_slider_pins(pos, white);
  update_slider_pins(pos, black);

  pos->fullmove += stm == black;
  pos->side ^= 1;
  pos->hash_keys.hash_key ^= keys.side_key;
}

// add move to the move list
void add_move(moves *move_list, int move) {
  // store move
  move_list->entry[move_list->count].move = move;

  // increment move count
  move_list->count++;
}

// generate only quiet moves
void generate_quiets(position_t *pos, moves *move_list, uint8_t no_reset) {
  if (!no_reset)
    move_list->count = 0;

  int source_square, target_square;
  uint64_t bitboard, attacks;

  const uint8_t PAWN_PC   = pos->side == white ? P : p;
  const uint8_t KNIGHT_PC = pos->side == white ? N : n;
  const uint8_t BISHOP_PC = pos->side == white ? B : b;
  const uint8_t ROOK_PC   = pos->side == white ? R : r;
  const uint8_t QUEEN_PC  = pos->side == white ? Q : q;
  const uint8_t KING_PC   = pos->side == white ? K : k;

  const uint64_t empty      = ~(pos->occupancies[white] | pos->occupancies[black]);
  const int      step       = pos->side == white ? -8  : 8;
  const uint64_t promo_mask = pos->side == white ? RANK7_MASK : RANK2_MASK;
  const uint64_t start_mask = pos->side == white ? RANK2_MASK : RANK7_MASK;
  const uint64_t pawns      = pos->bitboards[PAWN_PC];

  // pawns
  uint64_t singles = pos->side == white
      ? ((pawns & ~promo_mask) >> 8) & empty
      : ((pawns & ~promo_mask) << 8) & empty;
  while (singles) {
    target_square = __builtin_ctzll(singles);
    add_move(move_list, encode_move(target_square - step, target_square, QUIET));
    pop_bit(singles, target_square);
  }

  uint64_t doubles = pos->side == white
      ? (((pawns & start_mask) >> 8) & empty) >> 8 & empty
      : (((pawns & start_mask) << 8) & empty) << 8 & empty;
  while (doubles) {
    target_square = __builtin_ctzll(doubles);
    add_move(move_list, encode_move(target_square - 2 * step, target_square, DOUBLE_PUSH));
    pop_bit(doubles, target_square);
  }

  // knights
  bitboard = pos->bitboards[KNIGHT_PC];
  while (bitboard) {
    source_square = __builtin_ctzll(bitboard);
    attacks = knight_attacks[source_square] & empty;
    while (attacks) {
      target_square = __builtin_ctzll(attacks);
      add_move(move_list, encode_move(source_square, target_square, QUIET));
      pop_bit(attacks, target_square);
    }
    pop_bit(bitboard, source_square);
  }

  // bishops
  bitboard = pos->bitboards[BISHOP_PC];
  while (bitboard) {
    source_square = __builtin_ctzll(bitboard);
    attacks = get_bishop_attacks(source_square, pos->occupancies[white] | pos->occupancies[black]) & empty;
    while (attacks) {
      target_square = __builtin_ctzll(attacks);
      add_move(move_list, encode_move(source_square, target_square, QUIET));
      pop_bit(attacks, target_square);
    }
    pop_bit(bitboard, source_square);
  }

  // rooks
  bitboard = pos->bitboards[ROOK_PC];
  while (bitboard) {
    source_square = __builtin_ctzll(bitboard);
    attacks = get_rook_attacks(source_square, pos->occupancies[white] | pos->occupancies[black]) & empty;
    while (attacks) {
      target_square = __builtin_ctzll(attacks);
      add_move(move_list, encode_move(source_square, target_square, QUIET));
      pop_bit(attacks, target_square);
    }
    pop_bit(bitboard, source_square);
  }

  // queens
  bitboard = pos->bitboards[QUEEN_PC];
  while (bitboard) {
    source_square = __builtin_ctzll(bitboard);
    attacks = get_queen_attacks(source_square, pos->occupancies[white] | pos->occupancies[black]) & empty;
    while (attacks) {
      target_square = __builtin_ctzll(attacks);
      add_move(move_list, encode_move(source_square, target_square, QUIET));
      pop_bit(attacks, target_square);
    }
    pop_bit(bitboard, source_square);
  }

  // king moves
  bitboard = pos->bitboards[KING_PC];
  while (bitboard) {
    source_square = __builtin_ctzll(bitboard);
    attacks = king_attacks[source_square] & empty;
    while (attacks) {
      target_square = __builtin_ctzll(attacks);
      add_move(move_list, encode_move(source_square, target_square, QUIET));
      pop_bit(attacks, target_square);
    }
    pop_bit(bitboard, source_square);
  }

  // castling
  const uint8_t stm = pos->side;
  const uint8_t ksq = get_lsb(pos->bitboards[KING + 6 * stm]);
  for (uint8_t cs = 0; cs < 2; ++cs) {
    if (!(pos->castle & castle_bit(stm, cs)))
      continue;
    const uint8_t rsq   = pos->castle_rook_sq[stm][cs];
    const uint8_t kdest = castle_king_dest(stm, cs);
    const uint8_t rdest = castle_rook_dest(stm, cs);

    const uint64_t must_empty = ((between[ksq][kdest] | BB(kdest)) |
                                 (between[rsq][rdest] | BB(rdest))) &
                                ~(BB(ksq) | BB(rsq));
    if (must_empty & (pos->occupancies[white] | pos->occupancies[black]))
      continue;

    if (pos->blockers[stm] & BB(rsq))
      continue;
    uint64_t squares = between[ksq][kdest] | BB(ksq);
    uint8_t attacked = 0;
    while (squares) {
      const uint8_t sq = poplsb(&squares);
      if (is_square_attacked(pos, sq, stm ^ 1)) {
        attacked = 1;
        break;
      }
    }
    if (attacked)
      continue;

    add_move(move_list,
             encode_move(ksq, rsq, cs == 0 ? KING_CASTLE : QUEEN_CASTLE));
  }
}

void generate_noisy(position_t *pos, moves *move_list, uint8_t no_reset) {
  if (!no_reset)
    move_list->count = 0;

  int source_square, target_square;
  uint64_t bitboard, attacks;

  const uint8_t PAWN_PC   = pos->side == white ? P : p;
  const uint8_t KNIGHT_PC = pos->side == white ? N : n;
  const uint8_t BISHOP_PC = pos->side == white ? B : b;
  const uint8_t ROOK_PC   = pos->side == white ? R : r;
  const uint8_t QUEEN_PC  = pos->side == white ? Q : q;
  const uint8_t KING_PC   = pos->side == white ? K : k;

  const uint64_t enemy = pos->side == white ? pos->occupancies[black]
                                            : pos->occupancies[white];

  const int      step       = pos->side == white ? -8 : 8;
  const uint64_t promo_mask = pos->side == white ? RANK7_MASK : RANK2_MASK;

  // pawns
  bitboard = pos->bitboards[PAWN_PC];
  while (bitboard) {
    source_square = __builtin_ctzll(bitboard);
    target_square = source_square + step;

    if ((BB(source_square) & promo_mask) &&
        !get_bit(pos->occupancies[white] | pos->occupancies[black], target_square)) {
      add_move(move_list, encode_move(source_square, target_square, QUEEN_PROMOTION));
      add_move(move_list, encode_move(source_square, target_square, ROOK_PROMOTION));
      add_move(move_list, encode_move(source_square, target_square, BISHOP_PROMOTION));
      add_move(move_list, encode_move(source_square, target_square, KNIGHT_PROMOTION));
    }

    attacks = pawn_attacks[pos->side][source_square] & enemy;
    while (attacks) {
      target_square = __builtin_ctzll(attacks);
      if (BB(source_square) & promo_mask) {
        add_move(move_list, encode_move(source_square, target_square, QUEEN_CAPTURE_PROMOTION));
        add_move(move_list, encode_move(source_square, target_square, ROOK_CAPTURE_PROMOTION));
        add_move(move_list, encode_move(source_square, target_square, BISHOP_CAPTURE_PROMOTION));
        add_move(move_list, encode_move(source_square, target_square, KNIGHT_CAPTURE_PROMOTION));
      } else {
        add_move(move_list, encode_move(source_square, target_square, CAPTURE));
      }
      pop_bit(attacks, target_square);
    }

    if (pos->enpassant != no_sq) {
      uint64_t ep = pawn_attacks[pos->side][source_square] & (1ULL << pos->enpassant);
      if (ep)
        add_move(move_list, encode_move(source_square, __builtin_ctzll(ep), ENPASSANT_CAPTURE));
    }

    pop_bit(bitboard, source_square);
  }

  // knights
  bitboard = pos->bitboards[KNIGHT_PC];
  while (bitboard) {
    source_square = __builtin_ctzll(bitboard);
    attacks = knight_attacks[source_square] & enemy;
    while (attacks) {
      target_square = __builtin_ctzll(attacks);
      add_move(move_list, encode_move(source_square, target_square, CAPTURE));
      pop_bit(attacks, target_square);
    }
    pop_bit(bitboard, source_square);
  }

  // bishops
  bitboard = pos->bitboards[BISHOP_PC];
  while (bitboard) {
    source_square = __builtin_ctzll(bitboard);
    attacks = get_bishop_attacks(source_square, pos->occupancies[white] | pos->occupancies[black]) & enemy;
    while (attacks) {
      target_square = __builtin_ctzll(attacks);
      add_move(move_list, encode_move(source_square, target_square, CAPTURE));
      pop_bit(attacks, target_square);
    }
    pop_bit(bitboard, source_square);
  }

  // rooks
  bitboard = pos->bitboards[ROOK_PC];
  while (bitboard) {
    source_square = __builtin_ctzll(bitboard);
    attacks = get_rook_attacks(source_square, pos->occupancies[white] | pos->occupancies[black]) & enemy;
    while (attacks) {
      target_square = __builtin_ctzll(attacks);
      add_move(move_list, encode_move(source_square, target_square, CAPTURE));
      pop_bit(attacks, target_square);
    }
    pop_bit(bitboard, source_square);
  }

  // queens
  bitboard = pos->bitboards[QUEEN_PC];
  while (bitboard) {
    source_square = __builtin_ctzll(bitboard);
    attacks = get_queen_attacks(source_square, pos->occupancies[white] | pos->occupancies[black]) & enemy;
    while (attacks) {
      target_square = __builtin_ctzll(attacks);
      add_move(move_list, encode_move(source_square, target_square, CAPTURE));
      pop_bit(attacks, target_square);
    }
    pop_bit(bitboard, source_square);
  }

  // king
  bitboard = pos->bitboards[KING_PC];
  while (bitboard) {
    source_square = __builtin_ctzll(bitboard);
    attacks = king_attacks[source_square] & enemy;
    while (attacks) {
      target_square = __builtin_ctzll(attacks);
      add_move(move_list, encode_move(source_square, target_square, CAPTURE));
      pop_bit(attacks, target_square);
    }
    pop_bit(bitboard, source_square);
  }
}
