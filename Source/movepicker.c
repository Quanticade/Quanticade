#include "movepicker.h"
#include "enums.h"
#include "see.h"
#include "history.h"
#include "attacks.h"

extern int mvv[];
extern int MO_SEE_THRESHOLD;
extern int MO_QUIET_HIST_MULT;
extern int MO_CONT1_HIST_MULT;
extern int MO_CONT2_HIST_MULT;
extern int MO_CONT4_HIST_MULT;
extern int MO_PAWN_HIST_MULT;
extern int MO_CAPT_HIST_MULT;
extern int MO_MVV_MULT;

void init_picker(move_picker_t *picker, moves *move_list) {
    picker->move_list = move_list;
    picker->index = 0;
}

void score_moves(position_t *pos, thread_t *thread, searchstack_t *ss,
                 moves *move_list, uint16_t tt_move) {
    for (uint32_t i = 0; i < move_list->count; i++) {
        uint16_t move = move_list->entry[i].move;
        
        // TT move gets highest priority
        if (move == tt_move) {
            move_list->entry[i].score = 2000000000;
            continue;
        }
        
        uint8_t promoted_piece = get_move_promoted(pos->side, move);
        
        // Handle promotions
        if (promoted_piece) {
            switch (promoted_piece) {
            case q:
            case Q:
                move_list->entry[i].score = 1410000000;
                break;
            case n:
            case N:
                move_list->entry[i].score = 1400000000;
                break;
            default:
                move_list->entry[i].score = -800000000;
                break;
            }
            
            if (get_move_capture(move)) {
                if (SEE(pos, move, -MO_SEE_THRESHOLD)) {
                    continue;
                } else {
                    move_list->entry[i].score = -700000000;
                    continue;
                }
            } else {
                move_list->entry[i].score -= 100000;
                continue;
            }
        }
        
        move_list->entry[i].score = 0;
        
        // Score capture moves
        if (get_move_capture(move)) {
            int target_piece = get_move_enpassant(move) == 0
                                   ? pos->mailbox[get_move_target(move)]
                               : pos->side 
                                   ? pos->mailbox[get_move_target(move) - 8]
                                   : pos->mailbox[get_move_target(move) + 8];
            
            move_list->entry[i].score += mvv[target_piece % 6] * MO_MVV_MULT;
            move_list->entry[i].score +=
                thread->capture_history
                    [pos->mailbox[get_move_source(move)]]
                    [target_piece]
                    [get_move_source(move)]
                    [get_move_target(move)] * MO_CAPT_HIST_MULT;
            move_list->entry[i].score /= 1024;
            move_list->entry[i].score +=
                SEE(pos, move, -MO_SEE_THRESHOLD) ? 1000000000 : -1000000000;
            continue;
        }
        
        // Score quiet moves
        move_list->entry[i].score =
            thread->quiet_history
                [pos->side]
                [get_move_source(move)]
                [get_move_target(move)]
                [is_square_threatened(ss, get_move_source(move))]
                [is_square_threatened(ss, get_move_target(move))] *
                MO_QUIET_HIST_MULT +
            get_conthist_score(thread, pos, ss - 1, move) * MO_CONT1_HIST_MULT +
            get_conthist_score(thread, pos, ss - 2, move) * MO_CONT2_HIST_MULT +
            get_conthist_score(thread, pos, ss - 4, move) * MO_CONT4_HIST_MULT +
            thread->pawn_history
                [pos->hash_keys.pawn_key % 2048]
                [pos->mailbox[get_move_source(move)]]
                [get_move_target(move)] * MO_PAWN_HIST_MULT;
        move_list->entry[i].score /= 1024;
    }
}

uint16_t next_move(move_picker_t *picker) {
    if (picker->index >= picker->move_list->count)
        return 0;
    
    // Find best move in remaining moves
    uint16_t best = picker->index;
    for (uint16_t i = picker->index + 1; i < picker->move_list->count; ++i) {
        if (picker->move_list->entry[i].score > 
            picker->move_list->entry[best].score) {
            best = i;
        }
    }
    
    // Swap best to current position
    if (best != picker->index) {
        move_t temp = picker->move_list->entry[picker->index];
        picker->move_list->entry[picker->index] = picker->move_list->entry[best];
        picker->move_list->entry[best] = temp;
    }
    
    return picker->move_list->entry[picker->index++].move;
}