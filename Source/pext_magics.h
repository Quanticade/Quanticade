
#ifndef PEXT_MAGICS_H
#define PEXT_MAGICS_H

#include <stdint.h>

typedef struct pext_magic_s {
      uint64_t rook_mask;
      uint64_t bishop_mask;
      int rook_offset;
      int bishop_offset;
} pext_magic_t;

#endif
extern const uint16_t PextHeap[107648];
extern const pext_magic_t PextMagics[64];
extern const uint64_t RookEmptyAttacks[64];
extern const uint64_t BishopEmptyAttacks[64];
