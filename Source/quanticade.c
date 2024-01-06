// system headers
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef WIN64
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "consts.h"
#include "enums.h"
#include "macros.h"
#include "nnue/nnue.h"
#include "nnue_consts.h"
#include "structs.h"
#include "uci.h"
#include "utils.h"

engine_t engine;

// a bridge function to interact between search and GUI input
void communicate(search_info_t *search_info) {
  // if time is up break here
  if (search_info->timeset == 1 && get_time_ms() > search_info->stoptime) {
    // tell engine to stop calculating
    search_info->stopped = 1;
  }
}

/**********************************\
 ==================================

           Random numbers

 ==================================
\**********************************/

// generate 32-bit pseudo legal numbers
uint32_t get_random_U32_number(engine_t *engine) {
  // get current state
  uint32_t number = engine->random_state;

  // XOR shift algorithm
  number ^= number << 13;
  number ^= number >> 17;
  number ^= number << 5;

  // update random number state
  engine->random_state = number;

  // return random number
  return number;
}

// generate 64-bit pseudo legal numbers
uint64_t get_random_uint64_number(engine_t *engine) {
  // define 4 random numbers
  uint64_t n1, n2, n3, n4;

  // init random numbers slicing 16 bits from MS1B side
  n1 = (uint64_t)(get_random_U32_number(engine)) & 0xFFFF;
  n2 = (uint64_t)(get_random_U32_number(engine)) & 0xFFFF;
  n3 = (uint64_t)(get_random_U32_number(engine)) & 0xFFFF;
  n4 = (uint64_t)(get_random_U32_number(engine)) & 0xFFFF;

  // return random number
  return n1 | (n2 << 16) | (n3 << 32) | (n4 << 48);
}

// generate magic number candidate
uint64_t generate_magic_number(engine_t *engine) {
  return get_random_uint64_number(engine) & get_random_uint64_number(engine) &
         get_random_uint64_number(engine);
}

/**********************************\
 ==================================

          Bit manipulations

 ==================================
\**********************************/

// count bits within a bitboard (Brian Kernighan's way)
static inline uint8_t count_bits(uint64_t bitboard) {
  // bit counter
  uint8_t count = 0;

  // consecutively reset least significant 1st bit
  while (bitboard) {
    // increment count
    count++;

    // reset least significant 1st bit
    bitboard &= bitboard - 1;
  }

  // return bit count
  return count;
}

// get least significant 1st bit index
static inline uint8_t get_ls1b_index(uint64_t bitboard) {
  // make sure bitboard is not 0
  if (bitboard) {
    // count trailing bits before LS1B
    return count_bits((bitboard & -bitboard) - 1);
  }

  // otherwise
  else
    // return illegal index
    return -1;
}

/**********************************\
 ==================================

            Zobrist keys

 ==================================
\**********************************/

// init random hash keys
void init_random_keys(engine_t *engine) {
  // update pseudo random number state
  engine->random_state = 1804289383;

  // loop over piece codes
  for (int piece = P; piece <= k; piece++) {
    // loop over board squares
    for (int square = 0; square < 64; square++)
      // init random piece keys
      engine->keys.piece_keys[piece][square] = get_random_uint64_number(engine);
  }

  // loop over board squares
  for (int square = 0; square < 64; square++)
    // init random enpassant keys
    engine->keys.enpassant_keys[square] = get_random_uint64_number(engine);

  // loop over castling keys
  for (int index = 0; index < 16; index++)
    // init castling keys
    engine->keys.castle_keys[index] = get_random_uint64_number(engine);

  // init random side key
  engine->keys.side_key = get_random_uint64_number(engine);
}

// generate "almost" unique position ID aka hash key from scratch
uint64_t generate_hash_key(engine_t *engine) {
  // final hash key
  uint64_t final_key = 0ULL;

  // temp piece bitboard copy
  uint64_t bitboard;

  // loop over piece bitboards
  for (int piece = P; piece <= k; piece++) {
    // init piece bitboard copy
    bitboard = engine->board.bitboards[piece];

    // loop over the pieces within a bitboard
    while (bitboard) {
      // init square occupied by the piece
      int square = get_ls1b_index(bitboard);

      // hash piece
      final_key ^= engine->keys.piece_keys[piece][square];

      // pop LS1B
      pop_bit(bitboard, square);
    }
  }

  // if enpassant square is on board
  if (engine->board.enpassant != no_sq)
    // hash enpassant
    final_key ^= engine->keys.enpassant_keys[engine->board.enpassant];

  // hash castling rights
  final_key ^= engine->keys.castle_keys[engine->board.castle];

  // hash the side only if black is to move
  if (engine->board.side == black)
    final_key ^= engine->keys.side_key;

  // return generated hash key
  return final_key;
}

/**********************************\
 ==================================

           Input & Output

 ==================================
\**********************************/

// reset board variables
void reset_board(engine_t *engine) {
  // reset board position (bitboards)
  memset(engine->board.bitboards, 0ULL, sizeof(engine->board.bitboards));

  // reset occupancies (bitboards)
  memset(engine->board.occupancies, 0ULL, sizeof(engine->board.occupancies));

  // reset game state variables
  engine->board.side = 0;
  engine->board.enpassant = no_sq;
  engine->board.castle = 0;

  // reset repetition index
  engine->repetition_index = 0;

  engine->fifty = 0;

  // reset repetition table
  memset(engine->repetition_table, 0ULL, sizeof(engine->repetition_table));
}

// parse FEN string
void parse_fen(engine_t *engine, char *fen) {
  // prepare for new game
  reset_board(engine);

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
        set_bit(engine->board.bitboards[piece], square);

        // increment pointer to FEN string
        fen++;
      }

      // match empty square numbers within FEN string
      if (*fen >= '0' && *fen <= '9') {
        // init offset (convert char 0 to int 0)
        int offset = *fen - '0';

        // define piece variable
        int piece = -1;

        // loop over all piece bitboards
        for (int bb_piece = P; bb_piece <= k; bb_piece++) {
          // if there is a piece on current square
          if (get_bit(engine->board.bitboards[bb_piece], square))
            // get piece code
            piece = bb_piece;
        }

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
  (*fen == 'w') ? (engine->board.side = white) : (engine->board.side = black);

  // go to parsing castling rights
  fen += 2;

  // parse castling rights
  while (*fen != ' ') {
    switch (*fen) {
    case 'K':
      engine->board.castle |= wk;
      break;
    case 'Q':
      engine->board.castle |= wq;
      break;
    case 'k':
      engine->board.castle |= bk;
      break;
    case 'q':
      engine->board.castle |= bq;
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
    engine->board.enpassant = rank * 8 + file;
  }

  // no enpassant square
  else
    engine->board.enpassant = no_sq;

  // go to parsing half move counter (increment pointer to FEN string)
  fen++;

  // parse half move counter to init fifty move counter
  engine->fifty = atoi(fen);

  // loop over white pieces bitboards
  for (int piece = P; piece <= K; piece++)
    // populate white occupancy bitboard
    engine->board.occupancies[white] |= engine->board.bitboards[piece];

  // loop over black pieces bitboards
  for (int piece = p; piece <= k; piece++)
    // populate white occupancy bitboard
    engine->board.occupancies[black] |= engine->board.bitboards[piece];

  // init all occupancies
  engine->board.occupancies[both] |= engine->board.occupancies[white];
  engine->board.occupancies[both] |= engine->board.occupancies[black];

  // init hash key
  engine->board.hash_key = generate_hash_key(engine);
}

/**********************************\
 ==================================

              Attacks

 ==================================
\**********************************/
// TODO Move the global variables to struct
// rook magic numbers
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
void init_leapers_attacks(engine_t *engine) {
  // loop over 64 board squares
  for (int square = 0; square < 64; square++) {
    // init pawn attacks
    engine->attacks.pawn_attacks[white][square] =
        mask_pawn_attacks(white, square);
    engine->attacks.pawn_attacks[black][square] =
        mask_pawn_attacks(black, square);

    // init knight attacks
    engine->attacks.knight_attacks[square] = mask_knight_attacks(square);

    // init king attacks
    engine->attacks.king_attacks[square] = mask_king_attacks(square);
  }
}

// set occupancies
uint64_t set_occupancy(int index, int bits_in_mask, uint64_t attack_mask) {
  // occupancy map
  uint64_t occupancy = 0ULL;

  // loop over the range of bits within attack mask
  for (int count = 0; count < bits_in_mask; count++) {
    // get LS1B index of attacks mask
    int square = get_ls1b_index(attack_mask);

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
void init_sliders_attacks(engine_t *engine, int bishop) {
  // loop over 64 board squares
  for (int square = 0; square < 64; square++) {
    // init bishop & rook masks
    engine->masks.bishop_masks[square] = mask_bishop_attacks(square);
    engine->masks.rook_masks[square] = mask_rook_attacks(square);

    // init current mask
    uint64_t attack_mask = bishop ? engine->masks.bishop_masks[square]
                                  : engine->masks.rook_masks[square];

    // init relevant occupancy bit count
    int relevant_bits_count = count_bits(attack_mask);

    // init occupancy indicies
    int occupancy_indicies = (1 << relevant_bits_count);

    // loop over occupancy indicies
    for (int index = 0; index < occupancy_indicies; index++) {
      // bishop
      if (bishop) {
        // init current occupancy variation
        uint64_t occupancy =
            set_occupancy(index, relevant_bits_count, attack_mask);

        // init magic index
        int magic_index = (occupancy * bishop_magic_numbers[square]) >>
                          (64 - bishop_relevant_bits[square]);

        // init bishop attacks
        engine->attacks.bishop_attacks[square][magic_index] =
            bishop_attacks_on_the_fly(square, occupancy);
      }

      // rook
      else {
        // init current occupancy variation
        uint64_t occupancy =
            set_occupancy(index, relevant_bits_count, attack_mask);

        // init magic index
        int magic_index = (occupancy * rook_magic_numbers[square]) >>
                          (64 - rook_relevant_bits[square]);

        // init rook attacks
        engine->attacks.rook_attacks[square][magic_index] =
            rook_attacks_on_the_fly(square, occupancy);
      }
    }
  }
}

// get bishop attacks
static inline uint64_t get_bishop_attacks(engine_t *engine, int square,
                                          uint64_t occupancy) {
  // get bishop attacks assuming current board occupancy
  occupancy &= engine->masks.bishop_masks[square];
  occupancy *= bishop_magic_numbers[square];
  occupancy >>= 64 - bishop_relevant_bits[square];

  // return bishop attacks
  return engine->attacks.bishop_attacks[square][occupancy];
}

// get rook attacks
static inline uint64_t get_rook_attacks(engine_t *engine, int square,
                                        uint64_t occupancy) {
  // get rook attacks assuming current board occupancy
  occupancy &= engine->masks.rook_masks[square];
  occupancy *= rook_magic_numbers[square];
  occupancy >>= 64 - rook_relevant_bits[square];

  // return rook attacks
  return engine->attacks.rook_attacks[square][occupancy];
}

// get queen attacks
static inline uint64_t get_queen_attacks(engine_t *engine, int square,
                                         uint64_t occupancy) {
  // init result attacks bitboard
  uint64_t queen_attacks = 0ULL;

  // init bishop occupancies
  uint64_t bishop_occupancy = occupancy;

  // init rook occupancies
  uint64_t rook_occupancy = occupancy;

  // get bishop attacks assuming current board occupancy
  bishop_occupancy &= engine->masks.bishop_masks[square];
  bishop_occupancy *= bishop_magic_numbers[square];
  bishop_occupancy >>= 64 - bishop_relevant_bits[square];

  // get bishop attacks
  queen_attacks = engine->attacks.bishop_attacks[square][bishop_occupancy];

  // get rook attacks assuming current board occupancy
  rook_occupancy &= engine->masks.rook_masks[square];
  rook_occupancy *= rook_magic_numbers[square];
  rook_occupancy >>= 64 - rook_relevant_bits[square];

  // get rook attacks
  queen_attacks |= engine->attacks.rook_attacks[square][rook_occupancy];

  // return queen attacks
  return queen_attacks;
}

/**********************************\
 ==================================

           Move generator

 ==================================
\**********************************/

// is square current given attacked by the current given side
static inline int is_square_attacked(engine_t *engine, int square, int side) {
  // attacked by white pawns
  if ((side == white) && (engine->attacks.pawn_attacks[black][square] &
                          engine->board.bitboards[P]))
    return 1;

  // attacked by black pawns
  if ((side == black) && (engine->attacks.pawn_attacks[white][square] &
                          engine->board.bitboards[p]))
    return 1;

  // attacked by knights
  if (engine->attacks.knight_attacks[square] &
      ((side == white) ? engine->board.bitboards[N]
                       : engine->board.bitboards[n]))
    return 1;

  // attacked by bishops
  if (get_bishop_attacks(engine, square, engine->board.occupancies[both]) &
      ((side == white) ? engine->board.bitboards[B]
                       : engine->board.bitboards[b]))
    return 1;

  // attacked by rooks
  if (get_rook_attacks(engine, square, engine->board.occupancies[both]) &
      ((side == white) ? engine->board.bitboards[R]
                       : engine->board.bitboards[r]))
    return 1;

  // attacked by bishops
  if (get_queen_attacks(engine, square, engine->board.occupancies[both]) &
      ((side == white) ? engine->board.bitboards[Q]
                       : engine->board.bitboards[q]))
    return 1;

  // attacked by kings
  if (engine->attacks.king_attacks[square] &
      ((side == white) ? engine->board.bitboards[K]
                       : engine->board.bitboards[k]))
    return 1;

  // by default return false
  return 0;
}

/*
          binary move bits                               hexidecimal constants

    0000 0000 0000 0000 0011 1111    source square       0x3f
    0000 0000 0000 1111 1100 0000    target square       0xfc0
    0000 0000 1111 0000 0000 0000    piece               0xf000
    0000 1111 0000 0000 0000 0000    promoted piece      0xf0000
    0001 0000 0000 0000 0000 0000    capture flag        0x100000
    0010 0000 0000 0000 0000 0000    double push flag    0x200000
    0100 0000 0000 0000 0000 0000    enpassant flag      0x400000
    1000 0000 0000 0000 0000 0000    castling flag       0x800000
*/

// add move to the move list
static inline void add_move(moves *move_list, int move) {
  // store move
  move_list->moves[move_list->count] = move;

  // increment move count
  move_list->count++;
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

// print move list
void print_move_list(moves *move_list) {
  // do nothing on empty move list
  if (!move_list->count) {
    printf("\n     No move in the move list!\n");
    return;
  }

  printf("\n     move    piece     capture   double    enpass    castling\n\n");

  // loop over moves within a move list
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // init move
    int move = move_list->moves[move_count];

#ifdef WIN64
    // print move
    printf("      %s%s%c   %c         %d         %d         %d         %d\n",
           square_to_coordinates[get_move_source(move)],
           square_to_coordinates[get_move_target(move)],
           get_move_promoted(move) ? promoted_pieces[get_move_promoted(move)]
                                   : ' ',
           ascii_pieces[get_move_piece(move)], get_move_capture(move) ? 1 : 0,
           get_move_double(move) ? 1 : 0, get_move_enpassant(move) ? 1 : 0,
           get_move_castling(move) ? 1 : 0);
#else
    // print move
    printf("     %s%s%c   %s         %d         %d         %d         %d\n",
           square_to_coordinates[get_move_source(move)],
           square_to_coordinates[get_move_target(move)],
           get_move_promoted(move) ? promoted_pieces[get_move_promoted(move)]
                                   : ' ',
           unicode_pieces[get_move_piece(move)], get_move_capture(move) ? 1 : 0,
           get_move_double(move) ? 1 : 0, get_move_enpassant(move) ? 1 : 0,
           get_move_castling(move) ? 1 : 0);
#endif
  }

  // print total number of moves
  printf("\n\n     Total number of moves: %d\n\n", move_list->count);
}

// make move on chess board
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
                               ? get_ls1b_index(engine->board.bitboards[k])
                               : get_ls1b_index(engine->board.bitboards[K]),
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
          source_square = get_ls1b_index(bitboard);

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
          attacks =
              engine->attacks.pawn_attacks[engine->board.side][source_square] &
              engine->board.occupancies[black];

          // generate pawn captures
          while (attacks) {
            // init target square
            target_square = get_ls1b_index(attacks);

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
                engine->attacks
                    .pawn_attacks[engine->board.side][source_square] &
                (1ULL << engine->board.enpassant);

            // make sure enpassant capture available
            if (enpassant_attacks) {
              // init enpassant capture target square
              int target_enpassant = get_ls1b_index(enpassant_attacks);
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
          source_square = get_ls1b_index(bitboard);

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
          attacks =
              engine->attacks.pawn_attacks[engine->board.side][source_square] &
              engine->board.occupancies[white];

          // generate pawn captures
          while (attacks) {
            // init target square
            target_square = get_ls1b_index(attacks);

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
                engine->attacks
                    .pawn_attacks[engine->board.side][source_square] &
                (1ULL << engine->board.enpassant);

            // make sure enpassant capture available
            if (enpassant_attacks) {
              // init enpassant capture target square
              int target_enpassant = get_ls1b_index(enpassant_attacks);
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
        source_square = get_ls1b_index(bitboard);

        // init piece attacks in order to get set of target squares
        attacks =
            engine->attacks.knight_attacks[source_square] &
            ((engine->board.side == white) ? ~engine->board.occupancies[white]
                                           : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = get_ls1b_index(attacks);

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
        source_square = get_ls1b_index(bitboard);

        // init piece attacks in order to get set of target squares
        attacks =
            get_bishop_attacks(engine, source_square,
                               engine->board.occupancies[both]) &
            ((engine->board.side == white) ? ~engine->board.occupancies[white]
                                           : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = get_ls1b_index(attacks);

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
        source_square = get_ls1b_index(bitboard);

        // init piece attacks in order to get set of target squares
        attacks =
            get_rook_attacks(engine, source_square,
                             engine->board.occupancies[both]) &
            ((engine->board.side == white) ? ~engine->board.occupancies[white]
                                           : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = get_ls1b_index(attacks);

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
        source_square = get_ls1b_index(bitboard);

        // init piece attacks in order to get set of target squares
        attacks =
            get_queen_attacks(engine, source_square,
                              engine->board.occupancies[both]) &
            ((engine->board.side == white) ? ~engine->board.occupancies[white]
                                           : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = get_ls1b_index(attacks);

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
        source_square = get_ls1b_index(bitboard);

        // init piece attacks in order to get set of target squares
        attacks =
            engine->attacks.king_attacks[source_square] &
            ((engine->board.side == white) ? ~engine->board.occupancies[white]
                                           : ~engine->board.occupancies[black]);

        // loop over target squares available from generated attacks
        while (attacks) {
          // init target square
          target_square = get_ls1b_index(attacks);

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

/**********************************\
 ==================================

               Perft

 ==================================
\**********************************/

// perft driver
static inline void perft_driver(engine_t *engine, int depth) {
  // recursion escape condition
  if (depth == 0) {
    // increment nodes count (count reached positions)
    engine->search_info.nodes++;
    return;
  }

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(engine, move_list);

  // loop over generated moves
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    // make move
    if (!make_move(engine, move_list->moves[move_count], all_moves))
      // skip to the next move
      continue;

    // call perft driver recursively
    perft_driver(engine, depth - 1);

    // take back
    restore_board(engine->board.bitboards, engine->board.occupancies,
                  engine->board.side, engine->board.enpassant,
                  engine->board.castle, engine->fifty, engine->board.hash_key);
  }
}

// perft test
void perft_test(engine_t *engine, int depth) {
  printf("\n     Performance test\n\n");

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(engine, move_list);

  // init start time
  long start = get_time_ms();

  // loop over generated moves
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    // make move
    if (!make_move(engine, move_list->moves[move_count], all_moves))
      // skip to the next move
      continue;

    // cummulative nodes
    long cummulative_nodes = engine->search_info.nodes;

    // call perft driver recursively
    perft_driver(engine, depth - 1);

    // old nodes
    long old_nodes = engine->search_info.nodes - cummulative_nodes;

    // take back
    restore_board(engine->board.bitboards, engine->board.occupancies,
                  engine->board.side, engine->board.enpassant,
                  engine->board.castle, engine->fifty, engine->board.hash_key);

    // print move
    printf(
        "     move: %s%s%c  nodes: %ld\n",
        square_to_coordinates[get_move_source(move_list->moves[move_count])],
        square_to_coordinates[get_move_target(move_list->moves[move_count])],
        get_move_promoted(move_list->moves[move_count])
            ? promoted_pieces[get_move_promoted(move_list->moves[move_count])]
            : ' ',
        old_nodes);
  }

  // print results
  printf("\n    Depth: %d\n", depth);
#ifdef WIN64
  printf("    Nodes: %llu\n", engine->nodes);
  printf("     Time: %llu\n\n", get_time_ms() - start);
#else
  printf("    Nodes: %lu\n", engine->search_info.nodes);
  printf("     Time: %lu\n\n", get_time_ms() - start);
#endif
}

/**********************************\
 ==================================

             Evaluation

 ==================================
\**********************************/

// set file or rank mask
uint64_t set_file_rank_mask(int file_number, int rank_number) {
  // file or rank mask
  uint64_t mask = 0ULL;

  // loop over ranks
  for (int rank = 0; rank < 8; rank++) {
    // loop over files
    for (int file = 0; file < 8; file++) {
      // init square
      int square = rank * 8 + file;

      if (file_number != -1) {
        // on file match
        if (file == file_number) {
          // set bit on mask
          uint64_t temp = set_bit(mask, square);
          mask |= temp;
        }
      }

      else if (rank_number != -1) {
        // on rank match
        if (rank == rank_number) {
          // set bit on mask
          uint64_t temp = set_bit(mask, square);
          mask |= temp;
        }
      }
    }
  }

  // return mask
  return mask;
}

// init evaluation masks
void init_evaluation_masks(engine_t *engine) {
  /******** Init file masks ********/

  // loop over ranks
  for (int rank = 0; rank < 8; rank++) {
    // loop over files
    for (int file = 0; file < 8; file++) {
      // init square
      int square = rank * 8 + file;

      // init file mask for a current square
      engine->masks.file_masks[square] |= set_file_rank_mask(file, -1);

      // init rank mask for a current square
      engine->masks.rank_masks[square] |= set_file_rank_mask(-1, rank);

      // init isolated pawns masks for a current square
      engine->masks.isolated_masks[square] |= set_file_rank_mask(file - 1, -1);
      engine->masks.isolated_masks[square] |= set_file_rank_mask(file + 1, -1);

      // init white passed pawns mask for a current square
      engine->masks.white_passed_masks[square] |=
          set_file_rank_mask(file - 1, -1);
      engine->masks.white_passed_masks[square] |= set_file_rank_mask(file, -1);
      engine->masks.white_passed_masks[square] |=
          set_file_rank_mask(file + 1, -1);

      // init black passed pawns mask for a current square
      engine->masks.black_passed_masks[square] |=
          set_file_rank_mask(file - 1, -1);
      engine->masks.black_passed_masks[square] |= set_file_rank_mask(file, -1);
      engine->masks.black_passed_masks[square] |=
          set_file_rank_mask(file + 1, -1);

      // loop over redundant ranks
      for (int i = 0; i < (8 - rank); i++) {
        // reset redundant bits
        engine->masks.white_passed_masks[square] &=
            ~engine->masks.rank_masks[(7 - i) * 8 + file];
      }

      // loop over redundant ranks
      for (int i = 0; i < rank + 1; i++) {
        // reset redundant bits
        engine->masks.black_passed_masks[square] &=
            ~engine->masks.rank_masks[i * 8 + file];
      }
    }
  }
}

// get game phase score
static inline int get_game_phase_score(engine_t *engine) {
  /*
      The game phase score of the game is derived from the pieces
      (not counting pawns and kings) that are still on the board.
      The full material starting position game phase score is:

      4 * knight material score in the opening +
      4 * bishop material score in the opening +
      4 * rook material score in the opening +
      2 * queen material score in the opening
  */

  // white & black game phase scores
  int white_piece_scores = 0, black_piece_scores = 0;

  // loop over white pieces
  for (int piece = N; piece <= Q; piece++)
    white_piece_scores += count_bits(engine->board.bitboards[piece]) *
                          material_score[opening][piece];

  // loop over white pieces
  for (int piece = n; piece <= q; piece++)
    black_piece_scores += count_bits(engine->board.bitboards[piece]) *
                          -material_score[opening][piece];

  // return game phase score
  return white_piece_scores + black_piece_scores;
}

// position evaluation
int evaluate(engine_t *engine) {
  // get game phase score
  int game_phase_score = get_game_phase_score(engine);

  // game phase (opening, middle game, endgame)
  int game_phase = -1;

  // pick up game phase based on game phase score
  if (game_phase_score > opening_phase_score)
    game_phase = opening;
  else if (game_phase_score < endgame_phase_score)
    game_phase = endgame;
  else
    game_phase = middlegame;

  // static evaluation score
  int score = 0, score_opening = 0, score_endgame = 0;

  // current pieces bitboard copy
  uint64_t bitboard;

  // init piece & square
  int piece, square;

  // penalties
  int double_pawns = 0;

  // array of piece codes converted to Stockfish piece codes
  int pieces[33];

  // array of square indices converted to Stockfish square indices
  int squares[33];

  // pieces and squares current index to write next piece square pair at
  int index = 2;

  // loop over piece bitboards
  for (int bb_piece = P; bb_piece <= k; bb_piece++) {
    // init piece bitboard copy
    bitboard = engine->board.bitboards[bb_piece];

    // loop over pieces within a bitboard
    while (bitboard) {
      // init piece
      piece = bb_piece;

      // init square
      square = get_ls1b_index(bitboard);

      if (engine->nnue) {
        /*
                    Code to initialize pieces and squares arrays
                    to serve the purpose of direct probing of NNUE
                */

        // case white king
        if (piece == K) {
          /* convert white king piece code to stockfish piece code and
             store it at the first index of pieces array
          */
          pieces[0] = nnue_pieces[piece];

          /* convert white king square index to stockfish square index and
             store it at the first index of pieces array
          */
          squares[0] = nnue_squares[square];
        }

        // case black king
        else if (piece == k) {
          /* convert black king piece code to stockfish piece code and
             store it at the second index of pieces array
          */
          pieces[1] = nnue_pieces[piece];

          /* convert black king square index to stockfish square index and
             store it at the second index of pieces array
          */
          squares[1] = nnue_squares[square];
        }

        // all the rest pieces
        else {
          /*  convert all the rest of piece code with corresponding square codes
              to stockfish piece codes and square indices respectively
          */
          pieces[index] = nnue_pieces[piece];
          squares[index] = nnue_squares[square];
          index++;
        }
      } else {
        // get opening/endgame material score
        score_opening += material_score[opening][piece];
        score_endgame += material_score[endgame][piece];

        // score positional piece scores
        switch (piece) {
        // evaluate white pawns
        case P:
          // get opening/endgame positional score
          score_opening += positional_score[opening][PAWN][square];
          score_endgame += positional_score[endgame][PAWN][square];

          // double pawn penalty
          double_pawns = count_bits(engine->board.bitboards[P] &
                                    engine->masks.file_masks[square]);

          // on double pawns (tripple, etc)
          if (double_pawns > 1) {
            score_opening += (double_pawns - 1) * double_pawn_penalty_opening;
            score_endgame += (double_pawns - 1) * double_pawn_penalty_endgame;
          }

          // on isolated pawn
          if ((engine->board.bitboards[P] &
               engine->masks.isolated_masks[square]) == 0) {
            // give an isolated pawn penalty
            score_opening += isolated_pawn_penalty_opening;
            score_endgame += isolated_pawn_penalty_endgame;
          }
          // on passed pawn
          if ((engine->masks.white_passed_masks[square] &
               engine->board.bitboards[p]) == 0) {
            // give passed pawn bonus
            score_opening += passed_pawn_bonus[get_rank[square]];
            score_endgame += passed_pawn_bonus[get_rank[square]];
          }

          break;

        // evaluate white knights
        case N:
          // get opening/endgame positional score
          score_opening += positional_score[opening][KNIGHT][square];
          score_endgame += positional_score[endgame][KNIGHT][square];

          break;

        // evaluate white bishops
        case B:
          // get opening/endgame positional score
          score_opening += positional_score[opening][BISHOP][square];
          score_endgame += positional_score[endgame][BISHOP][square];

          // mobility
          score_opening +=
              (count_bits(get_bishop_attacks(engine, square,
                                             engine->board.occupancies[both])) -
               bishop_unit) *
              bishop_mobility_opening;
          score_endgame +=
              (count_bits(get_bishop_attacks(engine, square,
                                             engine->board.occupancies[both])) -
               bishop_unit) *
              bishop_mobility_endgame;
          break;

        // evaluate white rooks
        case R:
          // get opening/endgame positional score
          score_opening += positional_score[opening][ROOK][square];
          score_endgame += positional_score[endgame][ROOK][square];

          // semi open file
          if ((engine->board.bitboards[P] & engine->masks.file_masks[square]) ==
              0) {
            // add semi open file bonus
            score_opening += semi_open_file_score;
            score_endgame += semi_open_file_score;
          }

          // semi open file
          if (((engine->board.bitboards[P] | engine->board.bitboards[p]) &
               engine->masks.file_masks[square]) == 0) {
            // add semi open file bonus
            score_opening += open_file_score;
            score_endgame += open_file_score;
          }

          break;

        // evaluate white queens
        case Q:
          // get opening/endgame positional score
          score_opening += positional_score[opening][QUEEN][square];
          score_endgame += positional_score[endgame][QUEEN][square];

          // mobility
          score_opening +=
              (count_bits(get_queen_attacks(engine, square,
                                            engine->board.occupancies[both])) -
               queen_unit) *
              queen_mobility_opening;
          score_endgame +=
              (count_bits(get_queen_attacks(engine, square,
                                            engine->board.occupancies[both])) -
               queen_unit) *
              queen_mobility_endgame;
          break;

        // evaluate white king
        case K:
          // get opening/endgame positional score
          score_opening += positional_score[opening][KING][square];
          score_endgame += positional_score[endgame][KING][square];

          // semi open file
          if ((engine->board.bitboards[P] & engine->masks.file_masks[square]) ==
              0) {
            // add semi open file penalty
            score_opening -= semi_open_file_score;
            score_endgame -= semi_open_file_score;
          }

          // semi open file
          if (((engine->board.bitboards[P] | engine->board.bitboards[p]) &
               engine->masks.file_masks[square]) == 0) {
            // add semi open file penalty
            score_opening -= open_file_score;
            score_endgame -= open_file_score;
          }

          // king safety bonus
          score_opening += count_bits(engine->attacks.king_attacks[square] &
                                      engine->board.occupancies[white]) *
                           king_shield_bonus;
          score_endgame += count_bits(engine->attacks.king_attacks[square] &
                                      engine->board.occupancies[white]) *
                           king_shield_bonus;

          break;

        // evaluate black pawns
        case p:
          // get opening/endgame positional score
          score_opening -=
              positional_score[opening][PAWN][mirror_score[square]];
          score_endgame -=
              positional_score[endgame][PAWN][mirror_score[square]];

          // double pawn penalty
          double_pawns = count_bits(engine->board.bitboards[p] &
                                    engine->masks.file_masks[square]);

          // on double pawns (tripple, etc)
          if (double_pawns > 1) {
            score_opening -= (double_pawns - 1) * double_pawn_penalty_opening;
            score_endgame -= (double_pawns - 1) * double_pawn_penalty_endgame;
          }

          // on isolated pawn
          if ((engine->board.bitboards[p] &
               engine->masks.isolated_masks[square]) == 0) {
            // give an isolated pawn penalty
            score_opening -= isolated_pawn_penalty_opening;
            score_endgame -= isolated_pawn_penalty_endgame;
          }
          // on passed pawn
          if ((engine->masks.black_passed_masks[square] &
               engine->board.bitboards[P]) == 0) {
            // give passed pawn bonus
            score_opening -= passed_pawn_bonus[get_rank[square]];
            score_endgame -= passed_pawn_bonus[get_rank[square]];
          }

          break;

        // evaluate black knights
        case n:
          // get opening/endgame positional score
          score_opening -=
              positional_score[opening][KNIGHT][mirror_score[square]];
          score_endgame -=
              positional_score[endgame][KNIGHT][mirror_score[square]];

          break;

        // evaluate black bishops
        case b:
          // get opening/endgame positional score
          score_opening -=
              positional_score[opening][BISHOP][mirror_score[square]];
          score_endgame -=
              positional_score[endgame][BISHOP][mirror_score[square]];

          // mobility
          score_opening -=
              (count_bits(get_bishop_attacks(engine, square,
                                             engine->board.occupancies[both])) -
               bishop_unit) *
              bishop_mobility_opening;
          score_endgame -=
              (count_bits(get_bishop_attacks(engine, square,
                                             engine->board.occupancies[both])) -
               bishop_unit) *
              bishop_mobility_endgame;
          break;

        // evaluate black rooks
        case r:
          // get opening/endgame positional score
          score_opening -=
              positional_score[opening][ROOK][mirror_score[square]];
          score_endgame -=
              positional_score[endgame][ROOK][mirror_score[square]];

          // semi open file
          if ((engine->board.bitboards[p] & engine->masks.file_masks[square]) ==
              0) {
            // add semi open file bonus
            score_opening -= semi_open_file_score;
            score_endgame -= semi_open_file_score;
          }

          // semi open file
          if (((engine->board.bitboards[P] | engine->board.bitboards[p]) &
               engine->masks.file_masks[square]) == 0) {
            // add semi open file bonus
            score_opening -= open_file_score;
            score_endgame -= open_file_score;
          }

          break;

        // evaluate black queens
        case q:
          // get opening/endgame positional score
          score_opening -=
              positional_score[opening][QUEEN][mirror_score[square]];
          score_endgame -=
              positional_score[endgame][QUEEN][mirror_score[square]];

          // mobility
          score_opening -=
              (count_bits(get_queen_attacks(engine, square,
                                            engine->board.occupancies[both])) -
               queen_unit) *
              queen_mobility_opening;
          score_endgame -=
              (count_bits(get_queen_attacks(engine, square,
                                            engine->board.occupancies[both])) -
               queen_unit) *
              queen_mobility_endgame;
          break;

        // evaluate black king
        case k:
          // get opening/endgame positional score
          score_opening -=
              positional_score[opening][KING][mirror_score[square]];
          score_endgame -=
              positional_score[endgame][KING][mirror_score[square]];

          // semi open file
          if ((engine->board.bitboards[p] & engine->masks.file_masks[square]) ==
              0) {
            // add semi open file penalty
            score_opening += semi_open_file_score;
            score_endgame += semi_open_file_score;
          }

          // semi open file
          if (((engine->board.bitboards[P] | engine->board.bitboards[p]) &
               engine->masks.file_masks[square]) == 0) {
            // add semi open file penalty
            score_opening += open_file_score;
            score_endgame += open_file_score;
          }

          // king safety bonus
          score_opening -= count_bits(engine->attacks.king_attacks[square] &
                                      engine->board.occupancies[black]) *
                           king_shield_bonus;
          score_endgame -= count_bits(engine->attacks.king_attacks[square] &
                                      engine->board.occupancies[black]) *
                           king_shield_bonus;
          break;
        }
      }
      // pop ls1b
      pop_bit(bitboard, square);
    }
  }

  if (engine->nnue) {
    pieces[index] = 0;
    squares[index] = 0;
    return (int)(nnue_evaluate(engine->board.side, pieces, squares) *
                 (float)((100 - (float)engine->fifty) / 100));
  } else {
    /*
        Now in order to calculate interpolated score
        for a given game phase we use this formula
        (same for material and positional scores):

        (
          score_opening * game_phase_score +
          score_endgame * (opening_phase_score - game_phase_score)
        ) / opening_phase_score

        E.g. the score for pawn on d4 at phase say 5000 would be
        interpolated_score = (12 * 5000 + (-7) * (6192 - 5000)) / 6192 =
       8,342377261
    */

    // interpolate score in the middlegame
    if (game_phase == middlegame)
      score = (score_opening * game_phase_score +
               score_endgame * (opening_phase_score - game_phase_score)) /
              opening_phase_score;

    // return pure opening score in opening
    else if (game_phase == opening)
      score = score_opening;

    // return pure endgame score in endgame
    else if (game_phase == endgame)
      score = score_endgame;

    // return final evaluation based on side
    return (engine->board.side == white) ? score : -score;
  }
}

/**********************************\
 ==================================

               Search

 ==================================
\**********************************/

// clear TT (hash table)
void clear_hash_table(tt_t *hash_table) {
  memset(hash_table->hash_entry, 0,
         sizeof(tt_entry_t) * hash_table->num_of_entries);
  hash_table->current_age = 0;
}

// dynamically allocate memory for hash table
void init_hash_table(engine_t *engine, tt_t *hash_table, int mb) {
  // init hash size
  int hash_size = 0x100000 * mb;

  // init number of hash entries
  hash_table->num_of_entries = hash_size / sizeof(tt_entry_t);

  // free hash table if not empty
  if (hash_table->hash_entry != NULL) {
    printf("    Clearing hash memory...\n");

    // free hash table dynamic memory
    free(hash_table->hash_entry);
  }

  // allocate memory
  hash_table->hash_entry =
      (tt_entry_t *)malloc(hash_table->num_of_entries * sizeof(tt_entry_t));

  // if allocation has failed
  if (hash_table->hash_entry == NULL) {
    printf("    Couldn't allocate memory for hash table, trying %dMB...",
           mb / 2);

    // try to allocate with half size
    init_hash_table(engine, hash_table, mb / 2);
  }

  // if allocation succeeded
  else {
    // clear hash table
    clear_hash_table(hash_table);
  }
}

// read hash entry data
static inline int read_hash_entry(engine_t *engine, tt_t *hash_table, int alpha,
                                  int *move, int beta, int depth) {
  // create a TT instance pointer to particular hash entry storing
  // the scoring data for the current board position if available
  tt_entry_t *hash_entry =
      &hash_table
           ->hash_entry[engine->board.hash_key % hash_table->num_of_entries];

  // make sure we're dealing with the exact position we need
  if (hash_entry->hash_key == engine->board.hash_key) {
    // make sure that we match the exact depth our search is now at
    if (hash_entry->depth >= depth) {
      // extract stored score from TT entry
      int score = hash_entry->score;

      // retrieve score independent from the actual path
      // from root node (position) to current node (position)
      if (score < -mate_score)
        score += engine->ply;
      if (score > mate_score)
        score -= engine->ply;

      // match the exact (PV node) score
      if (hash_entry->flag == hash_flag_exact)
        // return exact (PV node) score
        return score;

      // match alpha (fail-low node) score
      if ((hash_entry->flag == hash_flag_alpha) && (score <= alpha))
        // return alpha (fail-low node) score
        return alpha;

      // match beta (fail-high node) score
      if ((hash_entry->flag == hash_flag_beta) && (score >= beta))
        // return beta (fail-high node) score
        return beta;
    }
    *move = hash_entry->move;
  }

  // if hash entry doesn't exist
  return no_hash_entry;
}

// write hash entry data
static inline void write_hash_entry(engine_t *engine, tt_t *hash_table,
                                    int score, int depth, int move,
                                    int hash_flag) {
  // create a TT instance pointer to particular hash entry storing
  // the scoring data for the current board position if available
  tt_entry_t *hash_entry =
      &hash_table
           ->hash_entry[engine->board.hash_key % hash_table->num_of_entries];

  if (!(hash_entry->hash_key == 0 ||
        (hash_entry->age < hash_table->current_age ||
         hash_entry->depth <= depth))) {
    return;
  }

  // store score independent from the actual path
  // from root node (position) to current node (position)
  if (score < -mate_score)
    score -= engine->ply;
  if (score > mate_score)
    score += engine->ply;

  // write hash entry data
  hash_entry->hash_key = engine->board.hash_key;
  hash_entry->score = score;
  hash_entry->flag = hash_flag;
  hash_entry->depth = depth;
  hash_entry->move = move;
  hash_entry->age = hash_table->current_age;
}

// enable PV move scoring
static inline void enable_pv_scoring(engine_t *engine, moves *move_list) {
  // disable following PV
  engine->follow_pv = 0;

  // loop over the moves within a move list
  for (uint32_t count = 0; count < move_list->count; count++) {
    // make sure we hit PV move
    if (engine->pv_table[0][engine->ply] == move_list->moves[count]) {
      // enable move scoring
      engine->score_pv = 1;

      // enable following PV
      engine->follow_pv = 1;
    }
  }
}

/*  =======================
         Move ordering
    =======================

    1. PV move
    2. Captures in MVV/LVA
    3. 1st killer move
    4. 2nd killer move
    5. History moves
    6. Unsorted moves
*/

// score moves
static inline int score_move(engine_t *engine, int move) {
  // if PV move scoring is allowed
  if (engine->score_pv) {
    // make sure we are dealing with PV move
    if (engine->pv_table[0][engine->ply] == move) {
      // disable score PV flag
      engine->score_pv = 0;

      // give PV move the highest score to search it first
      return 20000;
    }
  }

  // score capture move
  if (get_move_capture(move)) {
    // init target piece
    int target_piece = P;

    // pick up bitboard piece index ranges depending on side
    int start_piece, end_piece;

    // pick up side to move
    if (engine->board.side == white) {
      start_piece = p;
      end_piece = k;
    } else {
      start_piece = P;
      end_piece = K;
    }

    // loop over bitboards opposite to the current side to move
    for (int bb_piece = start_piece; bb_piece <= end_piece; bb_piece++) {
      // if there's a piece on the target square
      if (get_bit(engine->board.bitboards[bb_piece], get_move_target(move))) {
        // remove it from corresponding bitboard
        target_piece = bb_piece;
        break;
      }
    }

    // score move by MVV LVA lookup [source piece][target piece]
    return mvv_lva[get_move_piece(move)][target_piece] + 10000;
  }

  // score quiet move
  else {
    // score 1st killer move
    if (engine->killer_moves[0][engine->ply] == move)
      return 9000;

    // score 2nd killer move
    else if (engine->killer_moves[1][engine->ply] == move)
      return 8000;

    // score history move
    else
      return engine->history_moves[get_move_piece(move)][get_move_target(move)];
  }

  return 0;
}

// sort moves in descending order
static inline void sort_moves(engine_t *engine, moves *move_list, int move) {
  // move scores
  int move_scores[move_list->count];

  // score all the moves within a move list
  for (uint32_t count = 0; count < move_list->count; count++) {
    // if hash move available
    if (move == move_list->moves[count])
      // score move
      move_scores[count] = 30000;

    else
      // score move
      move_scores[count] = score_move(engine, move_list->moves[count]);
  }

  // loop over current move within a move list
  for (uint32_t current_move = 0; current_move < move_list->count;
       current_move++) {
    // loop over next move within a move list
    for (uint32_t next_move = current_move + 1; next_move < move_list->count;
         next_move++) {
      // compare current and next move scores
      if (move_scores[current_move] < move_scores[next_move]) {
        // swap scores
        int temp_score = move_scores[current_move];
        move_scores[current_move] = move_scores[next_move];
        move_scores[next_move] = temp_score;

        // swap moves
        int temp_move = move_list->moves[current_move];
        move_list->moves[current_move] = move_list->moves[next_move];
        move_list->moves[next_move] = temp_move;
      }
    }
  }
}

// print move scores
void print_move_scores(engine_t *engine, moves *move_list) {
  printf("     Move scores:\n\n");

  // loop over moves within a move list
  for (uint32_t count = 0; count < move_list->count; count++) {
    printf("     move: ");
    print_move(move_list->moves[count]);
    printf(" score: %d\n", score_move(engine, move_list->moves[count]));
  }
}

// position repetition detection
static inline int is_repetition(engine_t *engine) {
  // loop over repetition indices range
  for (uint32_t index = 0; index < engine->repetition_index; index++)
    // if we found the hash key same with a current
    if (engine->repetition_table[index] == engine->board.hash_key)
      // we found a repetition
      return 1;

  // if no repetition found
  return 0;
}

// quiescence search
static inline int quiescence(engine_t *engine, search_info_t *search_info,
                             int alpha, int beta) {
  // every 2047 nodes
  if ((search_info->nodes & 2047) == 0)
    // "listen" to the GUI/user input
    communicate(search_info);

  // we are too deep, hence there's an overflow of arrays relying on max ply
  // constant
  if (engine->ply > max_ply - 1)
    // evaluate position
    return evaluate(engine);

  // evaluate position
  int evaluation = evaluate(engine);

  // fail-hard beta cutoff
  if (evaluation >= beta) {
    // node (position) fails high
    return beta;
  }

  // found a better move
  if (evaluation > alpha) {
    // PV node (position)
    alpha = evaluation;
  }

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(engine, move_list);

  // sort moves
  sort_moves(engine, move_list, 0);

  // loop over moves within a movelist
  for (uint32_t count = 0; count < move_list->count; count++) {
    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    // increment ply
    engine->ply++;

    // increment repetition index & store hash key
    engine->repetition_index++;
    engine->repetition_table[engine->repetition_index] = engine->board.hash_key;

    // make sure to make only legal moves
    if (make_move(engine, move_list->moves[count], only_captures) == 0) {
      // decrement ply
      engine->ply--;

      // decrement repetition index
      engine->repetition_index--;

      // skip to next move
      continue;
    }

    // score current move
    int score = -quiescence(engine, search_info, -beta, -alpha);

    // decrement ply
    engine->ply--;

    // decrement repetition index
    engine->repetition_index--;

    // take move back
    restore_board(engine->board.bitboards, engine->board.occupancies,
                  engine->board.side, engine->board.enpassant,
                  engine->board.castle, engine->fifty, engine->board.hash_key);

    // return 0 if time is up
    if (search_info->stopped == 1) {
      return 0;
    }

    // found a better move
    if (score > alpha) {
      // PV node (position)
      alpha = score;

      // fail-hard beta cutoff
      if (score >= beta) {
        // node (position) fails high
        return beta;
      }
    }
  }

  // node (position) fails low
  return alpha;
}

// negamax alpha beta search
int negamax(engine_t *engine, search_info_t *search_info, tt_t *hash_table,
            int alpha, int beta, int depth) {
  // init PV length
  engine->pv_length[engine->ply] = engine->ply;

  // variable to store current move's score (from the static evaluation
  // perspective)
  int score;

  int move = 0;

  // define hash flag
  int hash_flag = hash_flag_alpha;

  // if position repetition occurs
  if ((is_repetition(engine) || engine->fifty >= 100) && engine->ply)
    // return draw score
    return 0;

  // a hack by Pedro Castro to figure out whether the current node is PV node or
  // not
  int pv_node = beta - alpha > 1;

  // read hash entry if we're not in a root ply and hash entry is available
  // and current node is not a PV node
  if (engine->ply &&
      (score = read_hash_entry(engine, hash_table, alpha, &move, beta,
                               depth)) != no_hash_entry &&
      pv_node == 0)
    // if the move has already been searched (hence has a value)
    // we just return the score for this move without searching it
    return score;

  // every 2047 nodes
  if ((search_info->nodes & 2047) == 0)
    // "listen" to the GUI/user input
    communicate(search_info);

  // recursion escapre condition
  if (depth == 0) {
    // run quiescence search
    return quiescence(engine, search_info, alpha, beta);
  }

  // we are too deep, hence there's an overflow of arrays relying on max ply
  // constant
  if (engine->ply > max_ply - 1)
    // evaluate position
    return evaluate(engine);

  // increment nodes count
  search_info->nodes++;

  // is king in check
  int in_check =
      is_square_attacked(engine,
                         (engine->board.side == white)
                             ? get_ls1b_index(engine->board.bitboards[K])
                             : get_ls1b_index(engine->board.bitboards[k]),
                         engine->board.side ^ 1);

  // increase search depth if the king has been exposed into a check
  if (in_check)
    depth++;

  // legal moves counter
  int legal_moves = 0;

  int static_eval = evaluate(engine);

  // evaluation pruning / static null move pruning
  if (depth < 3 && !pv_node && !in_check && abs(beta - 1) > -infinity + 100) {
    // get static evaluation score

    // define evaluation margin
    int eval_margin = 120 * depth;

    // evaluation margin substracted from static evaluation score fails high
    if (static_eval - eval_margin >= beta)
      // evaluation margin substracted from static evaluation score
      return static_eval - eval_margin;
  }

  // null move pruning
  if (depth >= 3 && in_check == 0 && engine->ply) {
    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    // increment ply
    engine->ply++;

    // increment repetition index & store hash key
    engine->repetition_index++;
    engine->repetition_table[engine->repetition_index] = engine->board.hash_key;

    // hash enpassant if available
    if (engine->board.enpassant != no_sq)
      engine->board.hash_key ^=
          engine->keys.enpassant_keys[engine->board.enpassant];

    // reset enpassant capture square
    engine->board.enpassant = no_sq;

    // switch the side, literally giving opponent an extra move to make
    engine->board.side ^= 1;

    // hash the side
    engine->board.hash_key ^= engine->keys.side_key;

    /* search moves with reduced depth to find beta cutoffs
       depth - 1 - R where R is a reduction limit */
    score = -negamax(engine, search_info, hash_table, -beta, -beta + 1,
                     depth - 1 - 2);

    // decrement ply
    engine->ply--;

    // decrement repetition index
    engine->repetition_index--;

    // restore board state
    restore_board(engine->board.bitboards, engine->board.occupancies,
                  engine->board.side, engine->board.enpassant,
                  engine->board.castle, engine->fifty, engine->board.hash_key);

    // return 0 if time is up
    if (search_info->stopped == 1) {
      return 0;
    }

    // fail-hard beta cutoff
    if (score >= beta)
      // node (position) fails high
      return beta;
  }

  // razoring
  if (!pv_node && !in_check && depth <= 3) {
    // get static eval and add first bonus
    score = static_eval + 125;

    // define new score
    int new_score;

    // static evaluation indicates a fail-low node
    if (score < beta) {
      // on depth 1
      if (depth == 1) {
        // get quiescence score
        new_score = quiescence(engine, search_info, alpha, beta);

        // return quiescence score if it's greater then static evaluation score
        return (new_score > score) ? new_score : score;
      }

      // add second bonus to static evaluation
      score += 175;

      // static evaluation indicates a fail-low node
      if (score < beta && depth <= 2) {
        // get quiescence score
        new_score = quiescence(engine, search_info, alpha, beta);

        // quiescence score indicates fail-low node
        if (new_score < beta)
          // return quiescence score if it's greater then static evaluation
          // score
          return (new_score > score) ? new_score : score;
      }
    }
  }

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(engine, move_list);

  // if we are now following PV line
  if (engine->follow_pv)
    // enable PV move scoring
    enable_pv_scoring(engine, move_list);

  // sort moves
  sort_moves(engine, move_list, move);

  // number of moves searched in a move list
  int moves_searched = 0;

  // loop over moves within a movelist
  for (uint32_t count = 0; count < move_list->count; count++) {
    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    // increment ply
    engine->ply++;

    // increment repetition index & store hash key
    engine->repetition_index++;
    engine->repetition_table[engine->repetition_index] = engine->board.hash_key;

    // make sure to make only legal moves
    if (make_move(engine, move_list->moves[count], all_moves) == 0) {
      // decrement ply
      engine->ply--;

      // decrement repetition index
      engine->repetition_index--;

      // skip to next move
      continue;
    }

    // increment legal moves
    legal_moves++;

    // full depth search
    if (moves_searched == 0) {
      // do normal alpha beta search
      score =
          -negamax(engine, search_info, hash_table, -beta, -alpha, depth - 1);
    }

    // late move reduction (LMR)
    else {
      // condition to consider LMR
      if (moves_searched >= full_depth_moves && depth >= reduction_limit &&
          in_check == 0 && get_move_capture(move_list->moves[count]) == 0 &&
          get_move_promoted(move_list->moves[count]) == 0) {
        // search current move with reduced depth:
        score = -negamax(engine, search_info, hash_table, -alpha - 1, -alpha,
                         depth - 2);
      }

      // hack to ensure that full-depth search is done
      else {
        score = alpha + 1;
      }

      // principle variation search PVS
      if (score > alpha) {
        /* Once you've found a move with a score that is between alpha and beta,
           the rest of the moves are searched with the goal of proving that they
           are all bad. It's possible to do this a bit faster than a search that
           worries that one of the remaining moves might be good. */
        search_info->nodes++;
        score = -negamax(engine, search_info, hash_table, -alpha - 1, -alpha,
                         depth - 1);

        /* If the algorithm finds out that it was wrong, and that one of the
           subsequent moves was better than the first PV move, it has to search
           again, in the normal alpha-beta manner.  This happens sometimes, and
           it's a waste of time, but generally not often enough to counteract
           the savings gained from doing the "bad move proof" search referred to
           earlier. */
        if ((score > alpha) && (score < beta)) {
          /* re-search the move that has failed to be proved to be bad
             with normal alpha beta score bounds*/
          score = -negamax(engine, search_info, hash_table, -beta, -alpha,
                           depth - 1);
        }
      }
    }

    // decrement ply
    engine->ply--;

    // decrement repetition index
    engine->repetition_index--;

    // take move back
    restore_board(engine->board.bitboards, engine->board.occupancies,
                  engine->board.side, engine->board.enpassant,
                  engine->board.castle, engine->fifty, engine->board.hash_key);

    // return infinity so we can deal with timeout in case we are doing
    // re-search
    if (search_info->stopped == 1) {
      return infinity;
    }

    // increment the counter of moves searched so far
    moves_searched++;

    // found a better move
    if (score > alpha) {
      // switch hash flag from storing score for fail-low node
      // to the one storing score for PV node
      hash_flag = hash_flag_exact;

      move = move_list->moves[count];

      // on quiet moves
      if (get_move_capture(move_list->moves[count]) == 0)
        // store history moves
        engine->history_moves[get_move_piece(move_list->moves[count])]
                             [get_move_target(move_list->moves[count])] +=
            depth;

      // PV node (position)
      alpha = score;

      // write PV move
      engine->pv_table[engine->ply][engine->ply] = move_list->moves[count];

      // loop over the next ply
      for (int next_ply = engine->ply + 1;
           next_ply < engine->pv_length[engine->ply + 1]; next_ply++)
        // copy move from deeper ply into a current ply's line
        engine->pv_table[engine->ply][next_ply] =
            engine->pv_table[engine->ply + 1][next_ply];

      // adjust PV length
      engine->pv_length[engine->ply] = engine->pv_length[engine->ply + 1];

      // fail-hard beta cutoff
      if (score >= beta) {
        // store hash entry with the score equal to beta
        write_hash_entry(engine, hash_table, beta, depth, move, hash_flag_beta);

        // on quiet moves
        if (get_move_capture(move_list->moves[count]) == 0) {
          // store killer moves
          engine->killer_moves[1][engine->ply] =
              engine->killer_moves[0][engine->ply];
          engine->killer_moves[0][engine->ply] = move_list->moves[count];
        }

        // node (position) fails high
        return beta;
      }
    }
  }

  // we don't have any legal moves to make in the current postion
  if (legal_moves == 0) {
    // king is in check
    if (in_check)
      // return mating score (assuming closest distance to mating position)
      return -mate_value + engine->ply;

    // king is not in check
    else
      // return stalemate score
      return 0;
  }

  // store hash entry with the score equal to alpha
  write_hash_entry(engine, hash_table, alpha, depth, move, hash_flag);

  // node (position) fails low
  return alpha;
}

// search position for the best move
void search_position(engine_t *engine, search_info_t *search_info,
                     tt_t *hash_table, int depth) {
  // search start time
  uint64_t start = get_time_ms();

  // define best score variable
  int score = 0;

  int pv_table_copy[max_ply][max_ply];
  int pv_length_copy[max_ply];

  uint8_t window_ok = 1;

  // reset nodes counter
  search_info->nodes = 0;

  // reset "time is up" flag
  search_info->stopped = 0;

  // reset follow PV flags
  engine->follow_pv = 0;
  engine->score_pv = 0;

  hash_table->current_age++;

  // clear helper data structures for search
  memset(engine->killer_moves, 0, sizeof(engine->killer_moves));
  memset(engine->history_moves, 0, sizeof(engine->history_moves));
  memset(engine->pv_table, 0, sizeof(engine->pv_table));
  memset(engine->pv_length, 0, sizeof(engine->pv_length));

  // define initial alpha beta bounds
  int alpha = -infinity;
  int beta = infinity;

  // iterative deepening
  for (int current_depth = 1; current_depth <= depth; current_depth++) {
    // if time is up
    if (search_info->stopped == 1) {
      // stop calculating and return best move so far
      break;
    }

    // enable follow PV flag
    engine->follow_pv = 1;

    // We should not save PV move from unfinished depth for example if depth 12
    // finishes and goes to search depth 13 now but this triggers window cutoff
    // we dont want the info from depth 13 as its incomplete and in case depth
    // 14 search doesnt finish in time we will at least have an full PV line
    // from depth 12
    if (window_ok) {
      memcpy(pv_table_copy, engine->pv_table, sizeof(engine->pv_table));
      memcpy(pv_length_copy, engine->pv_length, sizeof(engine->pv_length));
    }

    // find best move within a given position
    score =
        negamax(engine, search_info, hash_table, alpha, beta, current_depth);

    // Reset aspiration window OK flag back to 1
    window_ok = 1;

    // We hit an apspiration window cut-off before time ran out and we jumped to
    // another depth with wider search which we didnt finish
    if (score == infinity) {
      // Restore the saved best line
      memcpy(engine->pv_table, pv_table_copy, sizeof(pv_table_copy));
      memcpy(engine->pv_length, pv_length_copy, sizeof(pv_length_copy));
      // Break out of the loop without printing info about the unfinished depth
      break;
    }

    // we fell outside the window, so try again with a full-width window (and
    // the same depth)
    if ((score <= alpha) || (score >= beta)) {
      // Do a full window re-search
      alpha = -infinity;
      beta = infinity;
      window_ok = 0;
      current_depth--;
      continue;
    }

    // set up the window for the next iteration
    alpha = score - 50;
    beta = score + 50;

    // if PV is available
    if (engine->pv_length[0]) {
      // print search info
      uint64_t time = get_time_ms() - start;
      uint64_t nps = (search_info->nodes / fmax(time, 1)) * 100;
      if (score > -mate_value && score < -mate_score) {
#ifdef WIN64
        printf("info depth %d score mate %d nodes %llu nps %llu time %llu pv ",
               current_depth, -(score + mate_value) / 2 - 1, search_info->nodes,
               nps, time);
#else
        printf("info depth %d score mate %d nodes %lu nps %ld time %lu pv ",
               current_depth, -(score + mate_value) / 2 - 1, search_info->nodes,
               nps, time);
#endif
      }

      else if (score > mate_score && score < mate_value) {
#ifdef WIN64
        printf("info depth %d score mate %d nodes %llu nps %llu time %llu pv ",
               current_depth, (mate_value - score) / 2 + 1, search_info->nodes,
               nps, time);
#else
        printf("info depth %d score mate %d nodes %lu nps %ld time %lu pv ",
               current_depth, (mate_value - score) / 2 + 1, search_info->nodes,
               nps, time);
#endif
      }

      else {
#ifdef WIN64
        printf("info depth %d score cp %d nodes %llu nps %llu time %llu pv ",
               current_depth, score, search_info->nodes, nps, time);
#else
        printf("info depth %d score cp %d nodes %lu nps %ld time %lu pv ",
               current_depth, score, search_info->nodes, nps, time);
#endif
      }

      // loop over the moves within a PV line
      for (int count = 0; count < engine->pv_length[0]; count++) {
        // print PV move
        print_move(engine->pv_table[0][count]);
        printf(" ");
      }

      // print new line
      printf("\n");
    }
  }

  // print best move
  printf("bestmove ");
  if (engine->pv_table[0][0]) {
    print_move(engine->pv_table[0][0]);
  } else {
    printf("(none)");
  }
  printf("\n");
}

// reset time control variables
void reset_time_control(search_info_t *search_info) {
  // reset timing
  search_info->quit = 0;
  search_info->movestogo = 30;
  search_info->time = -1;
  search_info->inc = 0;
  search_info->starttime = 0;
  search_info->stoptime = 0;
  search_info->timeset = 0;
  search_info->stopped = 0;
}

/**********************************\
 ==================================

              Init all

 ==================================
\**********************************/

// init all variables
void init_all(engine_t *engine, tt_t *hash_table) {

  engine->nnue_file = calloc(21, 1);
  strlcpy(engine->nnue_file, "nn-eba324f53044.nnue", 21);

  // init leaper pieces attacks
  init_leapers_attacks(engine);

  // init slider pieces attacks
  init_sliders_attacks(engine, 1);
  init_sliders_attacks(engine, 0);

  // init random keys for hashing purposes
  init_random_keys(engine);

  // init evaluation masks
  init_evaluation_masks(engine);

  // init hash table with default 128 MB
  init_hash_table(engine, hash_table, 128);

  if (engine->nnue) {
    nnue_init("nn-eba324f53044.nnue");
  }
}

/**********************************\
 ==================================

             Main driver

 ==================================
\**********************************/

int main(void) {
  memset(&engine, 0, sizeof(engine));
  engine.board.enpassant = no_sq;
  engine.nnue = 1;
  engine.random_state = 1804289383;

  search_info_t search_info = {0};
  search_info.movestogo = 30;
  search_info.time = -1;

  tt_t hash_table = {NULL, 0, 0};

  // init all
  init_all(&engine, &hash_table);

  // connect to GUI
  uci_loop(&engine, &search_info, &hash_table);

  // free hash table memory on exit
  free(hash_table.hash_entry);

  return 0;
}
