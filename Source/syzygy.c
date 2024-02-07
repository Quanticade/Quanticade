#include "enums.h"
#include "structs.h"
#include "pyrrhic/tbprobe.h"
#include <stdio.h>

unsigned quant_probe_wdl(position_t *pos) {
  unsigned res = tb_probe_wdl(pos->occupancies[0], pos->occupancies[1],
                      pos->bitboards[k] | pos->bitboards[K], pos->bitboards[q] | pos->bitboards[Q], pos->bitboards[r] | pos->bitboards[R],
                      pos->bitboards[b] | pos->bitboards[B], pos->bitboards[n] | pos->bitboards[N], pos->bitboards[p] | pos->bitboards[P],
                      pos->enpassant == no_sq ? 0 : pos->enpassant, pos->side == white ? 1 : 0);
	return res;
}
