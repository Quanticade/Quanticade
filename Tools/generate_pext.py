#!/usr/bin/python3

# Generate pext/pdep attacks
# Writes to pext_magics.h and pext_magics.c in the current working dir.
# e.g., in Source/, run python3 /path/to/generate_pext.py

def pext(src: int, mask: int) -> int:
    b = 1
    result = 0
    while mask:
        lowest = mask & ~(mask - 1)
        mask ^= lowest
        if src & lowest:
            result |= b
        b <<= 1
    return result


def pdep(src: int, mask: int) -> int:
    b = 1
    result = 0
    while mask:
        lowest = mask & ~(mask - 1)
        mask ^= lowest
        if src & b:
            result |= lowest
        b <<= 1
    return result


def compute_ray(r: int, f: int, dr: int, df: int, occupied: int, rim_check: bool) -> int:
    ray = 0

    while True:
        r += dr
        f += df
        if (not (0 <= r <= 7 and 0 <= f <= 7)) or (rim_check and not (0 <= r + dr <= 7 and 0 <= f + df <= 7)):
            return ray
        b = 1 << (f + r * 8)
        ray |= b
        if b & occupied:
            return ray


def slider_attacks(r: int, f: int, occupied: int, deltas: list[tuple[int, int]]) -> int:
    result = 0
    for (dr, df) in deltas:
        result |= compute_ray(r, f, dr, df, occupied, False)
    return result


def slider_mask(r: int, f: int, occupied: int, deltas: list[tuple[int, int]]) -> int:
    result = 0
    for (dr, df) in deltas:
        result |= compute_ray(r, f, dr, df, occupied, True)
    return result


Rook = [(1,0),(-1,0),(0,1),(0,-1)]
Bishop = [(1,1),(1,-1),(-1,-1),(-1,1)]

header = """
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
"""
source = """
#include "pext_magics.h"

"""

def format_int(x):
    assert x >= 0
    if x >= 0x100000000:
        return hex(x) + "ULL"
    if x >= 0x80000000:
        return hex(x) + "U"
    h = hex(x)
    s = str(x)
    return s if len(h) > len(s) else h


def emit_array_definition(c_name: str, v: list, datatype: str, formatter):
    global header, source
    l = len(v)
    header += f"extern const {datatype} {c_name}[{l}];\n"

    init = ",".join(map(formatter, v))
    source += f"const {datatype} {c_name}[{l}] = {{ {init} }};\n"


magics = []
heap = []
rook_attacks = []
bishop_attacks = []


def generate_magics(r: int, f: int, sl: list[tuple[int,int]]):
    global magics, heap

    attacks = slider_attacks(r, f, 0, sl)
    mask = slider_mask(r, f, 0, sl)

    c = mask.bit_count()
    for idx in range(0, 1 << c):
        occupied = pdep(idx, mask)
        attacked = slider_attacks(r, f, occupied, sl)

        heap.append(pext(attacked, attacks))

    return mask


for r in range(8):
    for f in range(8):
        rook_start = len(heap)
        rook_mask = generate_magics(r, f, Rook)
        rook_attacks.append(slider_attacks(r, f, 0, Rook))

        bishop_start = len(heap)
        bishop_mask = generate_magics(r, f, Bishop)
        bishop_attacks.append(slider_attacks(r, f, 0, Bishop))

        magics.append((rook_mask, bishop_mask, rook_start, bishop_start))


def format_magic(t):
    rook_mask, bishop_mask, rook_offset, bishop_offset = map(format_int, t)
    return f"{{ {rook_mask}, {bishop_mask}, {rook_offset}, {bishop_offset} }}"


emit_array_definition("PextHeap", heap, "uint16_t", format_int)
emit_array_definition("PextMagics", magics, "pext_magic_t", format_magic)
emit_array_definition("RookEmptyAttacks", rook_attacks, "uint64_t", format_int)
emit_array_definition("BishopEmptyAttacks", bishop_attacks, "uint64_t", format_int)

with open("pext_magics.h", "w") as f:
    f.write(header)

with open("pext_magics.c", "w") as f:
    f.write(source)
