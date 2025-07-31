#include "attacks.h"
#include "bitboards.h"
#include "enums.h"
#include "move.h"
#include "structs.h"
#include <stdint.h>

uint64_t pawn_attacks[2][64];
uint64_t knight_attacks[64];
uint64_t king_attacks[64];
uint64_t bishop_attacks[64][512];
uint64_t rook_attacks[64][4096];
uint64_t bishop_masks[64];
uint64_t rook_masks[64];
uint64_t file_masks[64];
uint64_t rank_masks[64];
uint64_t isolated_masks[64];
uint64_t white_passed_masks[64];
uint64_t black_passed_masks[64];

const uint64_t not_a_file = 18374403900871474942ULL;
const uint64_t not_h_file = 9187201950435737471ULL;
const uint64_t not_hg_file = 4557430888798830399ULL;
const uint64_t not_ab_file = 18229723555195321596ULL;

const int bishop_relevant_bits[64] = {
    6, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 7, 7, 7, 7,
    5, 5, 5, 5, 7, 9, 9, 7, 5, 5, 5, 5, 7, 9, 9, 7, 5, 5, 5, 5, 7, 7,
    7, 7, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 6};

const int rook_relevant_bits[64] = {
    12, 11, 11, 11, 11, 11, 11, 12, 11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11, 11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11, 11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11, 12, 11, 11, 11, 11, 11, 11, 12};

uint64_t rook_magic_numbers[64] = {
    0x8a80104000800020ULL, 0x140002000100040ULL,  0x2801880a0017001ULL,
    0x100081001000420ULL,  0x200020010080420ULL,  0x3001c0002010008ULL,
    0x8480008002000100ULL, 0x2080088004402900ULL, 0x800098204000ULL,
    0x2024401000200040ULL, 0x100802000801000ULL,  0x120800800801000ULL,
    0x208808088000400ULL,  0x2802200800400ULL,    0x2200800100020080ULL,
    0x801000060821100ULL,  0x80044006422000ULL,   0x100808020004000ULL,
    0x12108a0010204200ULL, 0x140848010000802ULL,  0x481828014002800ULL,
    0x8094004002004100ULL, 0x4010040010010802ULL, 0x20008806104ULL,
    0x100400080208000ULL,  0x2040002120081000ULL, 0x21200680100081ULL,
    0x20100080080080ULL,   0x2000a00200410ULL,    0x20080800400ULL,
    0x80088400100102ULL,   0x80004600042881ULL,   0x4040008040800020ULL,
    0x440003000200801ULL,  0x4200011004500ULL,    0x188020010100100ULL,
    0x14800401802800ULL,   0x2080040080800200ULL, 0x124080204001001ULL,
    0x200046502000484ULL,  0x480400080088020ULL,  0x1000422010034000ULL,
    0x30200100110040ULL,   0x100021010009ULL,     0x2002080100110004ULL,
    0x202008004008002ULL,  0x20020004010100ULL,   0x2048440040820001ULL,
    0x101002200408200ULL,  0x40802000401080ULL,   0x4008142004410100ULL,
    0x2060820c0120200ULL,  0x1001004080100ULL,    0x20c020080040080ULL,
    0x2935610830022400ULL, 0x44440041009200ULL,   0x280001040802101ULL,
    0x2100190040002085ULL, 0x80c0084100102001ULL, 0x4024081001000421ULL,
    0x20030a0244872ULL,    0x12001008414402ULL,   0x2006104900a0804ULL,
    0x1004081002402ULL};

// bishop magic numbers
uint64_t bishop_magic_numbers[64] = {
    0x40040844404084ULL,   0x2004208a004208ULL,   0x10190041080202ULL,
    0x108060845042010ULL,  0x581104180800210ULL,  0x2112080446200010ULL,
    0x1080820820060210ULL, 0x3c0808410220200ULL,  0x4050404440404ULL,
    0x21001420088ULL,      0x24d0080801082102ULL, 0x1020a0a020400ULL,
    0x40308200402ULL,      0x4011002100800ULL,    0x401484104104005ULL,
    0x801010402020200ULL,  0x400210c3880100ULL,   0x404022024108200ULL,
    0x810018200204102ULL,  0x4002801a02003ULL,    0x85040820080400ULL,
    0x810102c808880400ULL, 0xe900410884800ULL,    0x8002020480840102ULL,
    0x220200865090201ULL,  0x2010100a02021202ULL, 0x152048408022401ULL,
    0x20080002081110ULL,   0x4001001021004000ULL, 0x800040400a011002ULL,
    0xe4004081011002ULL,   0x1c004001012080ULL,   0x8004200962a00220ULL,
    0x8422100208500202ULL, 0x2000402200300c08ULL, 0x8646020080080080ULL,
    0x80020a0200100808ULL, 0x2010004880111000ULL, 0x623000a080011400ULL,
    0x42008c0340209202ULL, 0x209188240001000ULL,  0x400408a884001800ULL,
    0x110400a6080400ULL,   0x1840060a44020800ULL, 0x90080104000041ULL,
    0x201011000808101ULL,  0x1a2208080504f080ULL, 0x8012020600211212ULL,
    0x500861011240000ULL,  0x180806108200800ULL,  0x4000020e01040044ULL,
    0x300000261044000aULL, 0x802241102020002ULL,  0x20906061210001ULL,
    0x5a84841004010310ULL, 0x4010801011c04ULL,    0xa010109502200ULL,
    0x4a02012000ULL,       0x500201010098b028ULL, 0x8040002811040900ULL,
    0x28000010020204ULL,   0x6000020202d0240ULL,  0x8918844842082200ULL,
    0x4010011029020020ULL};

// generate pawn attacks
uint64_t mask_pawn_attacks(int side, int square) {
  // result attacks bitboard
  uint64_t attacks = 0ULL;

  // piece bitboard
  uint64_t bitboard = 0ULL;

  // set piece on board
  set_bit(bitboard, square);

  // white pawns
  if (!side) {
    // generate pawn attacks
    if ((bitboard >> 7) & not_a_file)
      attacks |= (bitboard >> 7);
    if ((bitboard >> 9) & not_h_file)
      attacks |= (bitboard >> 9);
  }

  // black pawns
  else {
    // generate pawn attacks
    if ((bitboard << 7) & not_h_file)
      attacks |= (bitboard << 7);
    if ((bitboard << 9) & not_a_file)
      attacks |= (bitboard << 9);
  }

  // return attack map
  return attacks;
}

// generate knight attacks
uint64_t mask_knight_attacks(int square) {
  // result attacks bitboard
  uint64_t attacks = 0ULL;

  // piece bitboard
  uint64_t bitboard = 0ULL;

  // set piece on board
  set_bit(bitboard, square);

  // generate knight attacks
  if ((bitboard >> 17) & not_h_file)
    attacks |= (bitboard >> 17);
  if ((bitboard >> 15) & not_a_file)
    attacks |= (bitboard >> 15);
  if ((bitboard >> 10) & not_hg_file)
    attacks |= (bitboard >> 10);
  if ((bitboard >> 6) & not_ab_file)
    attacks |= (bitboard >> 6);
  if ((bitboard << 17) & not_a_file)
    attacks |= (bitboard << 17);
  if ((bitboard << 15) & not_h_file)
    attacks |= (bitboard << 15);
  if ((bitboard << 10) & not_ab_file)
    attacks |= (bitboard << 10);
  if ((bitboard << 6) & not_hg_file)
    attacks |= (bitboard << 6);

  // return attack map
  return attacks;
}

// generate king attacks
uint64_t mask_king_attacks(int square) {
  // result attacks bitboard
  uint64_t attacks = 0ULL;

  // piece bitboard
  uint64_t bitboard = 0ULL;

  // set piece on board
  set_bit(bitboard, square);

  // generate king attacks
  if (bitboard >> 8)
    attacks |= (bitboard >> 8);
  if ((bitboard >> 9) & not_h_file)
    attacks |= (bitboard >> 9);
  if ((bitboard >> 7) & not_a_file)
    attacks |= (bitboard >> 7);
  if ((bitboard >> 1) & not_h_file)
    attacks |= (bitboard >> 1);
  if (bitboard << 8)
    attacks |= (bitboard << 8);
  if ((bitboard << 9) & not_a_file)
    attacks |= (bitboard << 9);
  if ((bitboard << 7) & not_h_file)
    attacks |= (bitboard << 7);
  if ((bitboard << 1) & not_a_file)
    attacks |= (bitboard << 1);

  // return attack map
  return attacks;
}

// mask bishop attacks
uint64_t mask_bishop_attacks(int square) {
  // result attacks bitboard
  uint64_t attacks = 0ULL;

  // init ranks & files
  int r, f;

  // init target rank & files
  int tr = square / 8;
  int tf = square % 8;

  // mask relevant bishop occupancy bits
  for (r = tr + 1, f = tf + 1; r <= 6 && f <= 6; r++, f++)
    attacks |= (1ULL << (r * 8 + f));
  for (r = tr - 1, f = tf + 1; r >= 1 && f <= 6; r--, f++)
    attacks |= (1ULL << (r * 8 + f));
  for (r = tr + 1, f = tf - 1; r <= 6 && f >= 1; r++, f--)
    attacks |= (1ULL << (r * 8 + f));
  for (r = tr - 1, f = tf - 1; r >= 1 && f >= 1; r--, f--)
    attacks |= (1ULL << (r * 8 + f));

  // return attack map
  return attacks;
}

// mask rook attacks
uint64_t mask_rook_attacks(int square) {
  // result attacks bitboard
  uint64_t attacks = 0ULL;

  // init ranks & files
  int r, f;

  // init target rank & files
  int tr = square / 8;
  int tf = square % 8;

  // mask relevant rook occupancy bits
  for (r = tr + 1; r <= 6; r++)
    attacks |= (1ULL << (r * 8 + tf));
  for (r = tr - 1; r >= 1; r--)
    attacks |= (1ULL << (r * 8 + tf));
  for (f = tf + 1; f <= 6; f++)
    attacks |= (1ULL << (tr * 8 + f));
  for (f = tf - 1; f >= 1; f--)
    attacks |= (1ULL << (tr * 8 + f));

  // return attack map
  return attacks;
}

// generate bishop attacks on the fly
uint64_t bishop_attacks_on_the_fly(int square, uint64_t block) {
  // result attacks bitboard
  uint64_t attacks = 0ULL;

  // init ranks & files
  int r, f;

  // init target rank & files
  int tr = square / 8;
  int tf = square % 8;

  // generate bishop atacks
  for (r = tr + 1, f = tf + 1; r <= 7 && f <= 7; r++, f++) {
    attacks |= (1ULL << (r * 8 + f));
    if ((1ULL << (r * 8 + f)) & block)
      break;
  }

  for (r = tr - 1, f = tf + 1; r >= 0 && f <= 7; r--, f++) {
    attacks |= (1ULL << (r * 8 + f));
    if ((1ULL << (r * 8 + f)) & block)
      break;
  }

  for (r = tr + 1, f = tf - 1; r <= 7 && f >= 0; r++, f--) {
    attacks |= (1ULL << (r * 8 + f));
    if ((1ULL << (r * 8 + f)) & block)
      break;
  }

  for (r = tr - 1, f = tf - 1; r >= 0 && f >= 0; r--, f--) {
    attacks |= (1ULL << (r * 8 + f));
    if ((1ULL << (r * 8 + f)) & block)
      break;
  }

  // return attack map
  return attacks;
}

// generate rook attacks on the fly
uint64_t rook_attacks_on_the_fly(int square, uint64_t block) {
  // result attacks bitboard
  uint64_t attacks = 0ULL;

  // init ranks & files
  int r, f;

  // init target rank & files
  int tr = square / 8;
  int tf = square % 8;

  // generate rook attacks
  for (r = tr + 1; r <= 7; r++) {
    attacks |= (1ULL << (r * 8 + tf));
    if ((1ULL << (r * 8 + tf)) & block)
      break;
  }

  for (r = tr - 1; r >= 0; r--) {
    attacks |= (1ULL << (r * 8 + tf));
    if ((1ULL << (r * 8 + tf)) & block)
      break;
  }

  for (f = tf + 1; f <= 7; f++) {
    attacks |= (1ULL << (tr * 8 + f));
    if ((1ULL << (tr * 8 + f)) & block)
      break;
  }

  for (f = tf - 1; f >= 0; f--) {
    attacks |= (1ULL << (tr * 8 + f));
    if ((1ULL << (tr * 8 + f)) & block)
      break;
  }

  // return attack map
  return attacks;
}

// init leaper pieces attacks
void init_leapers_attacks(void) {
  // loop over 64 board squares
  for (int square = 0; square < 64; square++) {
    // init pawn attacks
    pawn_attacks[white][square] = mask_pawn_attacks(white, square);
    pawn_attacks[black][square] = mask_pawn_attacks(black, square);

    // init knight attacks
    knight_attacks[square] = mask_knight_attacks(square);

    // init king attacks
    king_attacks[square] = mask_king_attacks(square);
  }
}

// set occupancies
uint64_t set_occupancy(int index, int bits_in_mask, uint64_t attack_mask) {
  // occupancy map
  uint64_t occupancy = 0ULL;

  // loop over the range of bits within attack mask
  for (int count = 0; count < bits_in_mask; count++) {
    // get LS1B index of attacks mask
    int square = __builtin_ctzll(attack_mask);

    // pop LS1B in attack map
    pop_bit(attack_mask, square);

    // make sure occupancy is on board
    if (index & (1 << count))
      // populate occupancy map
      occupancy |= (1ULL << square);
  }

  // return occupancy map
  return occupancy;
}

// init slider piece's attack tables
void init_sliders_attacks(void) {
  // loop over 64 board squares
  for (int square = 0; square < 64; square++) {
    // init bishop & rook masks
    bishop_masks[square] = mask_bishop_attacks(square);
    rook_masks[square] = mask_rook_attacks(square);

    // init relevant occupancy bit count
    int bishop_relevant_bits_count = __builtin_popcountll(bishop_masks[square]);
    int rook_relevant_bits_count = __builtin_popcountll(rook_masks[square]);

    // init occupancy indicies
    int bishop_occupancy_indicies = (1 << bishop_relevant_bits_count);
    int rook_occupancy_indicies = (1 << rook_relevant_bits_count);

    // loop over occupancy indicies
    for (int index = 0; index < bishop_occupancy_indicies; index++) {
      // bishop
      // init current occupancy variation
      uint64_t occupancy =
          set_occupancy(index, bishop_relevant_bits_count, bishop_masks[square]);

      // init magic index
      int magic_index = (occupancy * bishop_magic_numbers[square]) >>
                        (64 - bishop_relevant_bits[square]);

      // init bishop attacks
      bishop_attacks[square][magic_index] =
          bishop_attacks_on_the_fly(square, occupancy);
    }

    // loop over occupancy indicies
    for (int index = 0; index < rook_occupancy_indicies; index++) {
      // rook
      // init current occupancy variation
      uint64_t occupancy =
          set_occupancy(index, rook_relevant_bits_count, rook_masks[square]);

      // init magic index
      int magic_index = (occupancy * rook_magic_numbers[square]) >>
                        (64 - rook_relevant_bits[square]);

      // init rook attacks
      rook_attacks[square][magic_index] =
          rook_attacks_on_the_fly(square, occupancy);
    }
  }
}

// is square current given attacked by the current given side
int is_square_attacked(position_t *pos, int square, int side) {
  // attacked by white pawns
  if ((side == white) &&
      (pawn_attacks[black][square] & pos->bitboards[P]))
    return 1;

  // attacked by black pawns
  if ((side == black) &&
      (pawn_attacks[white][square] & pos->bitboards[p]))
    return 1;

  // attacked by knights
  if (knight_attacks[square] & ((side == white) ? pos->bitboards[N]
                                                : pos->bitboards[n]))
    return 1;

  // attacked by bishops
  if (get_bishop_attacks(square, pos->occupancies[both]) &
      ((side == white) ? pos->bitboards[B]
                       : pos->bitboards[b]))
    return 1;

  // attacked by rooks
  if (get_rook_attacks(square, pos->occupancies[both]) &
      ((side == white) ? pos->bitboards[R]
                       : pos->bitboards[r]))
    return 1;

  // attacked by bishops
  if (get_queen_attacks(square, pos->occupancies[both]) &
      ((side == white) ? pos->bitboards[Q]
                       : pos->bitboards[q]))
    return 1;

  // attacked by kings
  if (king_attacks[square] & ((side == white) ? pos->bitboards[K]
                                              : pos->bitboards[k]))
    return 1;

  // by default return false
  return 0;
}

// Returns 1 if the move might give check
uint8_t might_give_check(position_t *pos, uint16_t mv) {
    uint8_t from = get_move_source(mv);
    uint8_t to = get_move_target(mv);
    uint8_t side = pos->side;
    uint8_t them = side ^ 1;

    // Simulate the occupancy after the move
    uint64_t new_occ = pos->occupancies[both];
    new_occ ^= (1ULL << from);
    new_occ ^= (1ULL << to);

    uint8_t piece = pos->mailbox[from] % 6;
    uint8_t king_sq = get_lsb(pos->bitboards[them == white ? K : k]);
    uint64_t attacks = 0ULL;

    switch (piece) {
        case PAWN:
            attacks = pawn_attacks[side][to];
            break;
        case KNIGHT:
            attacks = knight_attacks[to];
            break;
        case BISHOP:
            attacks = get_bishop_attacks(to, new_occ);
            break;
        case ROOK:
            attacks = get_rook_attacks(to, new_occ);
            break;
        case QUEEN:
            attacks = get_queen_attacks(to, new_occ);
            break;
        default:
            return 0;
    }

    return (attacks >> king_sq) & 1ULL;
}

uint8_t stm_in_check(position_t *pos) {
  return is_square_attacked(
    pos,
    (pos->side == white) ? __builtin_ctzll(pos->bitboards[K])
                         : __builtin_ctzll(pos->bitboards[k]),
    pos->side ^ 1);
}
