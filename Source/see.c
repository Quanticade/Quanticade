#include "attacks.h"
#include "enums.h"
#include "move.h"
#include "structs.h"

extern int SEEPieceValues[];

static inline int move_estimated_value(position_t *pos, int move) {

  // Start with the value of the piece on the target square
  int target_piece = pos->mailbox[get_move_target(move)] > 5
                         ? pos->mailbox[get_move_target(move)] - 6
                         : pos->mailbox[get_move_target(move)];
  int promoted_piece = get_move_promoted(pos->side, move);
  promoted_piece = promoted_piece > 5 ? promoted_piece - 6 : promoted_piece;

  int value = SEEPieceValues[target_piece];

  // Factor in the new piece's value and remove our promoted pawn
  if (is_move_promotion(move))
    value += SEEPieceValues[promoted_piece] - SEEPieceValues[PAWN];

  // Target square is encoded as empty for enpass moves
  else if (get_move_enpassant(move))
    value = SEEPieceValues[PAWN];

  // We encode Castle moves as KxR, so the initial step is wrong
  else if (get_move_castling(move))
    value = 0;

  return value;
}

static inline uint64_t all_attackers_to_square(position_t *pos, uint64_t occupied, int sq) {

  // When performing a static exchange evaluation we need to find all
  // attacks to a given square, but we also are given an updated occupied
  // bitboard, which will likely not match the actual board, as pieces are
  // removed during the iterations in the static exchange evaluation

  return (get_pawn_attacks(white, sq) & pos->bitboards[p]) |
         (get_pawn_attacks(black, sq) & pos->bitboards[P]) |
         (get_knight_attacks(sq) & (pos->bitboards[n] | pos->bitboards[N])) |
         (get_bishop_attacks(sq, occupied) &
          ((pos->bitboards[b] | pos->bitboards[B]) |
           (pos->bitboards[q] | pos->bitboards[Q]))) |
         (get_rook_attacks(sq, occupied) &
          ((pos->bitboards[r] | pos->bitboards[R]) |
           (pos->bitboards[q] | pos->bitboards[Q]))) |
         (get_king_attacks(sq) & (pos->bitboards[k] | pos->bitboards[K]));
}

int SEE(position_t *pos, int move, int threshold) {

  int from, to, enpassant, promotion, colour, balance, nextVictim;
  uint64_t bishops, rooks, occupied, attackers, myAttackers;

  // Unpack move information
  from = get_move_source(move);
  to = get_move_target(move);
  enpassant = get_move_enpassant(move);
  promotion = get_move_promoted(pos->side, move);

  // Next victim is moved piece or promotion type
  nextVictim = promotion ? promotion : pos->mailbox[from];
  nextVictim = nextVictim > 5 ? nextVictim - 6 : nextVictim;

  // Balance is the value of the move minus threshold. Function
  // call takes care for Enpass, Promotion and Castling moves.
  balance = move_estimated_value(pos, move) - threshold;

  // Best case still fails to beat the threshold
  if (balance < 0)
    return 0;

  // Worst case is losing the moved piece
  balance -= SEEPieceValues[nextVictim];

  // If the balance is positive even if losing the moved piece,
  // the exchange is guaranteed to beat the threshold.
  if (balance >= 0)
    return 1;

  // Grab sliders for updating revealed attackers
  bishops = pos->bitboards[b] | pos->bitboards[B] | pos->bitboards[q] |
            pos->bitboards[Q];
  rooks = pos->bitboards[r] | pos->bitboards[R] | pos->bitboards[q] |
          pos->bitboards[Q];

  // Let occupied suppose that the move was actually made
  occupied = pos->occupancies[both];
  occupied = (occupied ^ (1ull << from)) | (1ull << to);
  if (enpassant)
    occupied ^= (1ull << pos->enpassant);

  // Get all pieces which attack the target square. And with occupied
  // so that we do not let the same piece attack twice
  attackers = all_attackers_to_square(pos, occupied, to) & occupied;

  // Now our opponents turn to recapture
  colour = pos->side ^ 1;

  while (1) {

    // If we have no more attackers left we lose
    myAttackers = attackers & pos->occupancies[colour];
    if (myAttackers == 0ull) {
      break;
    }

    // Find our weakest piece to attack with
    for (nextVictim = PAWN; nextVictim <= QUEEN; nextVictim++) {
      if (myAttackers &
          (pos->bitboards[nextVictim] | pos->bitboards[nextVictim + 6])) {
        break;
      }
    }

    // Remove this attacker from the occupied
    occupied ^=
        (1ull << get_lsb(myAttackers & (pos->bitboards[nextVictim] |
                                        pos->bitboards[nextVictim + 6])));

    // A diagonal move may reveal bishop or queen attackers
    if (nextVictim == PAWN || nextVictim == BISHOP || nextVictim == QUEEN)
      attackers |= get_bishop_attacks(to, occupied) & bishops;

    // A vertical or horizontal move may reveal rook or queen attackers
    if (nextVictim == ROOK || nextVictim == QUEEN)
      attackers |= get_rook_attacks(to, occupied) & rooks;

    // Make sure we did not add any already used attacks
    attackers &= occupied;

    // Swap the turn
    colour = !colour;

    // Negamax the balance and add the value of the next victim
    balance = -balance - 1 - SEEPieceValues[nextVictim];

    // If the balance is non negative after giving away our piece then we win
    if (balance >= 0) {

      // As a slide speed up for move legality checking, if our last attacking
      // piece is a king, and our opponent still has attackers, then we've
      // lost as the move we followed would be illegal
      if (nextVictim == KING && (attackers & pos->occupancies[colour]))
        colour = colour ^ 1;

      break;
    }
  }

  // Side to move after the loop loses
  return pos->side != colour;
}
