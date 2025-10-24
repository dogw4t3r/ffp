#ifndef FFP_H
#define FFP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t U64;

typedef enum { WP, WR, WN, WB, WQ, WK, BP, BR, BN, BB, BQ, BK, PIECE_N } Piece;
typedef enum { BLACK=0, WHITE=1 } Side;

enum {
    MF_QUIET=0,
    MF_CAPTURE=1<<0,
    MF_PROMO=1<<1,
    MF_ENPASSANT=1<<2,
    MF_CASTLE=1<<3,
    MF_DOUBLE=1<<4
};

typedef struct {
    int from, to, piece, promo, captured, flags;
} Move;

typedef struct {
    Move list[256];
    int count;
} MoveList;

typedef struct {
    U64 bb[PIECE_N];
    U64 occ_white, occ_black, occ_all;
    Side side;
    int castling;
    int ep_square;
    int halfmove_clock;
    int fullmove_number;
} Position;

typedef struct {
    int castling, ep_square, halfmove_clock, fullmove_number, captured;
} Undo;

typedef struct {
    int max_depth;              /* Maximum search depth 0 = default */
    int time_ms;                /* Maximum thinking time in milliseconds 0 = unlimited */
    uint64_t node_limit;        /* Maximum number of nodes to visit 0 = unlimited */
    const volatile bool *stop;  /* Optional external stop flag */
} SearchLimits;

typedef struct {
    Move best_move;
    int depth_reached;
    int score;
    uint64_t nodes;
    bool aborted;
} SearchResult;

extern const char *FFP_FEN_STARTPOS;

void ffp_position_clear(Position *pos);
bool ffp_position_from_fen(Position *pos, const char *fen);
void ffp_position_set_start(Position *pos);
bool ffp_position_to_fen(const Position *pos, char *buffer, size_t length);

void ffp_generate_pseudo_legal(const Position *pos, MoveList *ml);
void ffp_generate_legal(const Position *pos, MoveList *ml);
int ffp_generate_legal_array(const Position *pos, Move *moves, int max_moves);

void ffp_make_move(Position *pos, const Move move, Undo *undo);
void ffp_unmake_move(Position *pos, const Move move, const Undo *undo);

bool ffp_is_square_attacked(const Position *pos, int square, Side by);

SearchResult ffp_search(Position *pos, const SearchLimits *limits);

void ffp_move_to_string(const Move *move, char out[6]);
bool ffp_move_from_string(const Position *pos, const char *uci, Move *out_move);

void ffp_print_board(const Position *pos);

#ifdef __cplusplus
}
#endif

#endif /* FFP_H */