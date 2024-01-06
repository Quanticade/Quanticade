#ifndef UCI_H
#define UCI_H

#include "structs.h"
void uci_loop(engine_t *engine, search_info_t* search_info, tt_t* hash_table);

#endif
