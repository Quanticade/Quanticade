#ifndef MOVE_H
#define MOVE_H

#include "structs.h"
#include <stdint.h>

#define QUIET 0
#define DOUBLE_PUSH 1
#define KING_CASTLE 2
#define QUEEN_CASTLE 3
#define CAPTURE 4
#define ENPASSANT_CAPTURE 5
#define KNIGHT_PROMOTION 8
#define BISHOP_PROMOTION 9
#define ROOK_PROMOTION 10
#define QUEEN_PROMOTION 11
#define KNIGHT_CAPTURE_PROMOTION 12
#define BISHOP_CAPTURE_PROMOTION 13
#define ROOK_CAPTURE_PROMOTION 14
#define QUEEN_CAPTURE_PROMOTION 15

uint16_t encode_move(uint16_t source, uint16_t target, uint16_t move_type);
uint8_t is_move_promotion(uint16_t move);
uint8_t get_move_source(uint16_t move);
uint8_t get_move_target(uint16_t move);
uint8_t get_move_piece(uint8_t mailbox[64], uint16_t move);
uint8_t get_move_promoted(uint8_t side, uint16_t move);
uint8_t get_move_capture(uint16_t move);
uint8_t get_move_double(uint16_t move);
uint8_t get_move_enpassant(uint16_t move);
uint8_t get_move_castling(uint16_t move);

#endif
