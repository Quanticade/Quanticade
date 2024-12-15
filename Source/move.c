#include "move.h"
#include "enums.h"
#include "structs.h"
#include <stdint.h>

uint16_t encode_move(uint16_t source, uint16_t target, uint16_t move_type) {
    return move_type | (target << 4) | (source << 10);
}

uint8_t get_move_source(uint16_t move) {
    return move >> 10;
}

uint8_t get_move_target(uint16_t move) {
    return (move >> 4) & 63;
}

uint8_t is_move_promotion(uint16_t move) {
    return ((move & 8) << 12) >> 15;
}

uint8_t get_move_promoted(uint8_t side, uint16_t move) {
    if (!is_move_promotion(move)) {
        return 0;
    }
    uint8_t piece = (move & 3) + 1;
    return side == black ? piece : piece + 6;
}

uint8_t get_move_capture(uint16_t move) {
    return (move & 4) >> 2;
}

uint8_t get_move_double(uint16_t move) {
    return (move & 15) == 1;
}

uint8_t get_move_piece(uint8_t mailbox[64], uint16_t move) {
    uint8_t piece = mailbox[get_move_source(move)];
    return piece;
}

uint8_t get_move_enpassant(uint16_t move) {
    return ((move & 4) && (move & 1) && ((move & 8) == 0));
}

uint8_t get_move_castling(uint16_t move) {
    uint8_t castling = move & 15;
    return (castling > 1 && castling < 4) ? castling : 0;
}
