// ffp.c — Far-From-Perfect chess engine (single file)
// Features: bitboards, FEN loader, pseudo-legal + legal movegen (incl. EP/castling/promotions),
// make/unmake with undo, perft, simple alpha-beta (material only), minimal UCI loop.
//
// Board indexing: a8 = 0, ..., h8 = 7, a7 = 8, ..., a1 = 56, ..., h1 = 63.
//
// Build:  gcc -O2 -Wall -Wextra -o ffp ffp.c
// Run:    ./ffp --help   |   ./ffp --uci   |   ./ffp --perft 4

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>

typedef uint64_t U64;

#ifndef __has_builtin
  #define __has_builtin(x) 0
#endif

#if defined(__GNUC__) || __has_builtin(__builtin_ctzll)
  #define LSB_INDEX(b) __builtin_ctzll(b)
#else
static inline int LSB_INDEX(U64 b){ // portable fallback
    static const int idx64[64] = {
        63, 0, 58, 1, 59, 47, 53, 2,
        60, 39, 48, 27, 54, 33, 42, 3,
        61, 51, 37, 40, 49, 18, 28, 20,
        55, 30, 34, 11, 43, 14, 22, 4,
        62, 57, 46, 52, 38, 26, 32, 41,
        50, 36, 17, 19, 29, 10, 13, 21,
        56, 45, 25, 31, 35, 16, 9, 12,
        44, 24, 15, 8, 23, 7, 6, 5
    };
    return idx64[((b & -b) * 0x07EDD5E59A4E28C2ULL) >> 58];
}
#endif

// Files
static const U64 FILE_A = 0x0101010101010101ULL;
static const U64 FILE_H = 0x8080808080808080ULL;

// Ranks
static inline U64 RANK_MASK(int n){ return (n>=1 && n<=8) ? (0xFFULL << (8*(8-n))) : 0ULL; }

// Enums
typedef enum { WP, WR, WN, WB, WQ, WK, BP, BR, BN, BB, BQ, BK, PIECE_N } Piece;
typedef enum { BLACK=0, WHITE=1 } Side;

// Piece chars
static const char PIECE_CHARS[PIECE_N] = {'P','R','N','B','Q','K','p','r','n','b','q','k'};
static inline int type_of_piece(int p){ return p % 6; }

// Bit helpers
static inline U64 get_bit(U64 b, int s){ return b & (1ULL<<s); }
static inline void set_bit(U64 *b, int s){ *b |=  (1ULL<<s); }
static inline void pop_bit(U64 *b, int s){ *b &= ~(1ULL<<s); }

static inline int popcount64(U64 x){
#if defined(__GNUC__) || __has_builtin(__builtin_popcountll)
    return __builtin_popcountll(x);
#else
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    return (int)((((x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL) * 0x0101010101010101ULL) >> 56);
#endif
}

// Shifts (a8..h1)
static inline U64 shift_east (U64 bb){ return (bb & ~FILE_H) << 1; }
static inline U64 shift_west (U64 bb){ return (bb & ~FILE_A) >> 1; }
static inline U64 shift_south(U64 bb){ return bb << 8;  }
static inline U64 shift_north(U64 bb){ return bb >> 8;  }
static inline U64 shift_ne   (U64 bb){ return (bb & ~FILE_H) >> 7; }
static inline U64 shift_nw   (U64 bb){ return (bb & ~FILE_A) >> 9; }
static inline U64 shift_se   (U64 bb){ return (bb & ~FILE_H) << 9; }
static inline U64 shift_sw   (U64 bb){ return (bb & ~FILE_A) << 7; }

// Position
typedef struct {
    U64 bb[PIECE_N];
    U64 occ_white, occ_black, occ_all;
    Side side;
    int castling;     // 1=K,2=Q,4=k,8=q
    int ep_square;    // -1 if none
    int halfmove_clock;
    int fullmove_number;
} Position;

static inline void update_occupancy(Position *pos){
    pos->occ_white = pos->bb[WP]|pos->bb[WR]|pos->bb[WN]|pos->bb[WB]|pos->bb[WQ]|pos->bb[WK];
    pos->occ_black = pos->bb[BP]|pos->bb[BR]|pos->bb[BN]|pos->bb[BB]|pos->bb[BQ]|pos->bb[BK];
    pos->occ_all   = pos->occ_white | pos->occ_black;
}

// Attacks (independent)
static inline U64 king_attacks_from(U64 src){
    U64 a=0; a |= shift_north(src)|shift_south(src)|shift_east(src)|shift_west(src);
    a |= shift_ne(src)|shift_nw(src)|shift_se(src)|shift_sw(src); return a;
}
static inline U64 knight_attacks_from(U64 src){
    U64 a=0, nn=shift_north(shift_north(src)), ss=shift_south(shift_south(src));
    U64 ee=shift_east(shift_east(src)), ww=shift_west(shift_west(src));
    a |= shift_east(nn)|shift_west(nn)|shift_east(ss)|shift_west(ss)
       | shift_north(ee)|shift_south(ee)|shift_north(ww)|shift_south(ww);
    return a;
}
static inline U64 white_pawn_attacks(U64 p){ return shift_nw(p)|shift_ne(p); }
static inline U64 black_pawn_attacks(U64 p){ return shift_sw(p)|shift_se(p); }

// Rays (blocker-aware)
static inline U64 ray_north(U64 s,U64 o){U64 a=0,x=s;while((x=shift_north(x))){a|=x;if(x&o)break;}return a;}
static inline U64 ray_south(U64 s,U64 o){U64 a=0,x=s;while((x=shift_south(x))){a|=x;if(x&o)break;}return a;}
static inline U64 ray_east (U64 s,U64 o){U64 a=0,x=s;while((x=shift_east (x))){a|=x;if(x&o)break;}return a;}
static inline U64 ray_west (U64 s,U64 o){U64 a=0,x=s;while((x=shift_west (x))){a|=x;if(x&o)break;}return a;}
static inline U64 ray_ne   (U64 s,U64 o){U64 a=0,x=s;while((x=shift_ne   (x))){a|=x;if(x&o)break;}return a;}
static inline U64 ray_nw   (U64 s,U64 o){U64 a=0,x=s;while((x=shift_nw   (x))){a|=x;if(x&o)break;}return a;}
static inline U64 ray_se   (U64 s,U64 o){U64 a=0,x=s;while((x=shift_se   (x))){a|=x;if(x&o)break;}return a;}
static inline U64 ray_sw   (U64 s,U64 o){U64 a=0,x=s;while((x=shift_sw   (x))){a|=x;if(x&o)break;}return a;}

static inline U64 rook_attacks_from  (U64 s,U64 o){return ray_north(s,o)|ray_south(s,o)|ray_east(s,o)|ray_west(s,o);}
static inline U64 bishop_attacks_from(U64 s,U64 o){return ray_ne(s,o)|ray_nw(s,o)|ray_se(s,o)|ray_sw(s,o);}
static inline U64 queen_attacks_from (U64 s,U64 o){return rook_attacks_from(s,o)|bishop_attacks_from(s,o);}

// Attack test
static inline bool square_attacked(const Position *pos, int sq, Side by){
    U64 t = 1ULL<<sq, occ = pos->occ_all;
    if (by==WHITE){
        if (white_pawn_attacks(pos->bb[WP]) & t) return true;
        U64 r=pos->bb[WN]; while(r){int s=LSB_INDEX(r); if(knight_attacks_from(1ULL<<s)&t) return true; r&=r-1;}
        U64 bq=pos->bb[WB]|pos->bb[WQ]; U64 x=bq; while(x){int s=LSB_INDEX(x); if(bishop_attacks_from(1ULL<<s,occ)&t) return true; x&=x-1;}
        U64 rq=pos->bb[WR]|pos->bb[WQ]; x=rq; while(x){int s=LSB_INDEX(x); if(rook_attacks_from(1ULL<<s,occ)&t)   return true; x&=x-1;}
        if (king_attacks_from(pos->bb[WK]) & t) return true;
    } else {
        if (black_pawn_attacks(pos->bb[BP]) & t) return true;
        U64 r=pos->bb[BN]; while(r){int s=LSB_INDEX(r); if(knight_attacks_from(1ULL<<s)&t) return true; r&=r-1;}
        U64 bq=pos->bb[BB]|pos->bb[BQ]; U64 x=bq; while(x){int s=LSB_INDEX(x); if(bishop_attacks_from(1ULL<<s,occ)&t) return true; x&=x-1;}
        U64 rq=pos->bb[BR]|pos->bb[BQ]; x=rq; while(x){int s=LSB_INDEX(x); if(rook_attacks_from(1ULL<<s,occ)&t)   return true; x&=x-1;}
        if (king_attacks_from(pos->bb[BK]) & t) return true;
    }
    return false;
}

// Moves
enum { MF_QUIET=0, MF_CAPTURE=1<<0, MF_PROMO=1<<1, MF_ENPASSANT=1<<2, MF_CASTLE=1<<3, MF_DOUBLE=1<<4 };

typedef struct { int from,to,piece,promo,captured,flags; } Move;
typedef struct { Move list[256]; int count; } MoveList;

static inline void add_move(MoveList *ml,int from,int to,int piece,int captured,int promo,int flags){
    ml->list[ml->count++] = (Move){from,to,piece,promo,captured,flags};
}

// Pseudo-legal generator
static void generate_pseudo_legal(const Position *pos, MoveList *ml){
    ml->count = 0;
    const Side us = pos->side;
    const U64 own = (us==WHITE)? pos->occ_white : pos->occ_black;
    const U64 opp = (us==WHITE)? pos->occ_black : pos->occ_white;
    const U64 empty = ~pos->occ_all;

    // Pawns
    if (us==WHITE){
        U64 pawns = pos->bb[WP];
        U64 single = shift_north(pawns) & empty;
        U64 doublep = shift_north(single & RANK_MASK(3)) & empty; // from R2 via R3 to R4
        U64 promo_push = single & RANK_MASK(8);
        U64 quiet_push = single & ~RANK_MASK(8);
        // Quiet pushes
        for (U64 q=quiet_push; q; q&=q-1){ int to=LSB_INDEX(q); int from=to+8; add_move(ml,from,to,WP,-1,-1,MF_QUIET); }
        // Double pushes
        for (U64 d=doublep; d; d&=d-1){ int to=LSB_INDEX(d); int from=to+16; add_move(ml,from,to,WP,-1,-1,MF_DOUBLE); }
        // Promo pushes (to=rank8); promos: Q,R,B,N = WQ,WR,WB,WN
        for (U64 pp=promo_push; pp; pp&=pp-1){ int to=LSB_INDEX(pp); int from=to+8;
            add_move(ml,from,to,WP,-1,WQ,MF_PROMO);
            add_move(ml,from,to,WP,-1,WR,MF_PROMO);
            add_move(ml,from,to,WP,-1,WB,MF_PROMO);
            add_move(ml,from,to,WP,-1,WN,MF_PROMO);
        }
        // Captures
        U64 capL = shift_nw(pawns) & opp, capR = shift_ne(pawns) & opp;
        U64 promoL = capL & RANK_MASK(8), promoR = capR & RANK_MASK(8);
        U64 normL  = capL & ~RANK_MASK(8), normR  = capR & ~RANK_MASK(8);
        for (U64 c=normL; c; c&=c-1){ int to=LSB_INDEX(c); int from=to+9; int cap=-1;
            for (int p=BP;p<=BK;p++) if (get_bit(pos->bb[p],to)){ cap=p; break; }
            add_move(ml,from,to,WP,cap,-1,MF_CAPTURE);
        }
        for (U64 c=normR; c; c&=c-1){ int to=LSB_INDEX(c); int from=to+7; int cap=-1;
            for (int p=BP;p<=BK;p++) if (get_bit(pos->bb[p],to)){ cap=p; break; }
            add_move(ml,from,to,WP,cap,-1,MF_CAPTURE);
        }
        for (U64 c=promoL; c; c&=c-1){ int to=LSB_INDEX(c); int from=to+9; int cap=-1;
            for (int p=BP;p<=BK;p++) if (get_bit(pos->bb[p],to)){ cap=p; break; }
            add_move(ml,from,to,WP,cap,WQ,MF_CAPTURE|MF_PROMO);
            add_move(ml,from,to,WP,cap,WR,MF_CAPTURE|MF_PROMO);
            add_move(ml,from,to,WP,cap,WB,MF_CAPTURE|MF_PROMO);
            add_move(ml,from,to,WP,cap,WN,MF_CAPTURE|MF_PROMO);
        }
        for (U64 c=promoR; c; c&=c-1){ int to=LSB_INDEX(c); int from=to+7; int cap=-1;
            for (int p=BP;p<=BK;p++) if (get_bit(pos->bb[p],to)){ cap=p; break; }
            add_move(ml,from,to,WP,cap,WQ,MF_CAPTURE|MF_PROMO);
            add_move(ml,from,to,WP,cap,WR,MF_CAPTURE|MF_PROMO);
            add_move(ml,from,to,WP,cap,WB,MF_CAPTURE|MF_PROMO);
            add_move(ml,from,to,WP,cap,WN,MF_CAPTURE|MF_PROMO);
        }
        // En passant
        if (pos->ep_square!=-1){
            U64 ep = 1ULL<<pos->ep_square;
            if (shift_nw(pawns)&ep){ int to=pos->ep_square; int from=to+9; add_move(ml,from,to,WP,BP,-1,MF_ENPASSANT|MF_CAPTURE); }
            if (shift_ne(pawns)&ep){ int to=pos->ep_square; int from=to+7; add_move(ml,from,to,WP,BP,-1,MF_ENPASSANT|MF_CAPTURE); }
        }
    } else { // BLACK
        U64 pawns = pos->bb[BP];
        U64 single = shift_south(pawns) & empty;
        U64 doublep = shift_south(single & RANK_MASK(6)) & empty;
        U64 promo_push = single & RANK_MASK(1);
        U64 quiet_push = single & ~RANK_MASK(1);
        for (U64 q=quiet_push; q; q&=q-1){ int to=LSB_INDEX(q); int from=to-8; add_move(ml,from,to,BP,-1,-1,MF_QUIET); }
        for (U64 d=doublep; d; d&=d-1){ int to=LSB_INDEX(d); int from=to-16; add_move(ml,from,to,BP,-1,-1,MF_DOUBLE); }
        for (U64 pp=promo_push; pp; pp&=pp-1){ int to=LSB_INDEX(pp); int from=to-8;
            add_move(ml,from,to,BP,-1,BQ,MF_PROMO);
            add_move(ml,from,to,BP,-1,BR,MF_PROMO);
            add_move(ml,from,to,BP,-1,BB,MF_PROMO);
            add_move(ml,from,to,BP,-1,BN,MF_PROMO);
        }
        U64 capL = shift_sw(pawns) & opp, capR = shift_se(pawns) & opp;
        U64 promoL = capL & RANK_MASK(1), promoR = capR & RANK_MASK(1);
        U64 normL  = capL & ~RANK_MASK(1), normR  = capR & ~RANK_MASK(1);
        for (U64 c=normL; c; c&=c-1){ int to=LSB_INDEX(c); int from=to-9; int cap=-1;
            for (int p=WP;p<=WK;p++) if (get_bit(pos->bb[p],to)){ cap=p; break; }
            add_move(ml,from,to,BP,cap,-1,MF_CAPTURE);
        }
        for (U64 c=normR; c; c&=c-1){ int to=LSB_INDEX(c); int from=to-7; int cap=-1;
            for (int p=WP;p<=WK;p++) if (get_bit(pos->bb[p],to)){ cap=p; break; }
            add_move(ml,from,to,BP,cap,-1,MF_CAPTURE);
        }
        for (U64 c=promoL; c; c&=c-1){ int to=LSB_INDEX(c); int from=to-9; int cap=-1;
            for (int p=WP;p<=WK;p++) if (get_bit(pos->bb[p],to)){ cap=p; break; }
            add_move(ml,from,to,BP,cap,BQ,MF_CAPTURE|MF_PROMO);
            add_move(ml,from,to,BP,cap,BR,MF_CAPTURE|MF_PROMO);
            add_move(ml,from,to,BP,cap,BB,MF_CAPTURE|MF_PROMO);
            add_move(ml,from,to,BP,cap,BN,MF_CAPTURE|MF_PROMO);
        }
        for (U64 c=promoR; c; c&=c-1){ int to=LSB_INDEX(c); int from=to-7; int cap=-1;
            for (int p=WP;p<=WK;p++) if (get_bit(pos->bb[p],to)){ cap=p; break; }
            add_move(ml,from,to,BP,cap,BQ,MF_CAPTURE|MF_PROMO);
            add_move(ml,from,to,BP,cap,BR,MF_CAPTURE|MF_PROMO);
            add_move(ml,from,to,BP,cap,BB,MF_CAPTURE|MF_PROMO);
            add_move(ml,from,to,BP,cap,BN,MF_CAPTURE|MF_PROMO);
        }
        if (pos->ep_square!=-1){
            U64 ep = 1ULL<<pos->ep_square;
            if (shift_sw(pawns)&ep){ int to=pos->ep_square; int from=to-9; add_move(ml,from,to,BP,WP,-1,MF_ENPASSANT|MF_CAPTURE); }
            if (shift_se(pawns)&ep){ int to=pos->ep_square; int from=to-7; add_move(ml,from,to,BP,WP,-1,MF_ENPASSANT|MF_CAPTURE); }
        }
    }

    // Knights
    if (us==WHITE){
        for (U64 r=pos->bb[WN]; r; r&=r-1){
            int s=LSB_INDEX(r); U64 moves=knight_attacks_from(1ULL<<s)&~own;
            for (U64 m=moves; m; m&=m-1){ int to=LSB_INDEX(m); int cap=-1;
                if (get_bit(opp,to)){
                    cap = get_bit(pos->bb[BP],to)?BP:get_bit(pos->bb[BR],to)?BR:get_bit(pos->bb[BN],to)?BN:get_bit(pos->bb[BB],to)?BB:get_bit(pos->bb[BQ],to)?BQ:BK;
                }
                add_move(ml,s,to,WN,cap,-1,cap!=-1?MF_CAPTURE:MF_QUIET);
            }
        }
    } else {
        for (U64 r=pos->bb[BN]; r; r&=r-1){
            int s=LSB_INDEX(r); U64 moves=knight_attacks_from(1ULL<<s)&~own;
            for (U64 m=moves; m; m&=m-1){ int to=LSB_INDEX(m); int cap=-1;
                if (get_bit(opp,to)){
                    cap = get_bit(pos->bb[WP],to)?WP:get_bit(pos->bb[WR],to)?WR:get_bit(pos->bb[WN],to)?WN:get_bit(pos->bb[WB],to)?WB:get_bit(pos->bb[WQ],to)?WQ:WK;
                }
                add_move(ml,s,to,BN,cap,-1,cap!=-1?MF_CAPTURE:MF_QUIET);
            }
        }
    }

    // Bishops
    if (us==WHITE){
        for (U64 r=pos->bb[WB]; r; r&=r-1){
            int s=LSB_INDEX(r); U64 moves=bishop_attacks_from(1ULL<<s,pos->occ_all)&~own;
            for (U64 m=moves; m; m&=m-1){ int to=LSB_INDEX(m); int cap=-1;
                if (get_bit(opp,to)){
                    cap = get_bit(pos->bb[BP],to)?BP:get_bit(pos->bb[BR],to)?BR:get_bit(pos->bb[BN],to)?BN:get_bit(pos->bb[BB],to)?BB:get_bit(pos->bb[BQ],to)?BQ:BK;
                }
                add_move(ml,s,to,WB,cap,-1,cap!=-1?MF_CAPTURE:MF_QUIET);
            }
        }
    } else {
        for (U64 r=pos->bb[BB]; r; r&=r-1){
            int s=LSB_INDEX(r); U64 moves=bishop_attacks_from(1ULL<<s,pos->occ_all)&~own;
            for (U64 m=moves; m; m&=m-1){ int to=LSB_INDEX(m); int cap=-1;
                if (get_bit(opp,to)){
                    cap = get_bit(pos->bb[WP],to)?WP:get_bit(pos->bb[WR],to)?WR:get_bit(pos->bb[WN],to)?WN:get_bit(pos->bb[WB],to)?WB:get_bit(pos->bb[WQ],to)?WQ:WK;
                }
                add_move(ml,s,to,BB,cap,-1,cap!=-1?MF_CAPTURE:MF_QUIET);
            }
        }
    }

    // Rooks
    if (us==WHITE){
        for (U64 r=pos->bb[WR]; r; r&=r-1){
            int s=LSB_INDEX(r); U64 moves=rook_attacks_from(1ULL<<s,pos->occ_all)&~own;
            for (U64 m=moves; m; m&=m-1){ int to=LSB_INDEX(m); int cap=-1;
                if (get_bit(opp,to)){
                    cap = get_bit(pos->bb[BP],to)?BP:get_bit(pos->bb[BR],to)?BR:get_bit(pos->bb[BN],to)?BN:get_bit(pos->bb[BB],to)?BB:get_bit(pos->bb[BQ],to)?BQ:BK;
                }
                add_move(ml,s,to,WR,cap,-1,cap!=-1?MF_CAPTURE:MF_QUIET);
            }
        }
    } else {
        for (U64 r=pos->bb[BR]; r; r&=r-1){
            int s=LSB_INDEX(r); U64 moves=rook_attacks_from(1ULL<<s,pos->occ_all)&~own;
            for (U64 m=moves; m; m&=m-1){ int to=LSB_INDEX(m); int cap=-1;
                if (get_bit(opp,to)){
                    cap = get_bit(pos->bb[WP],to)?WP:get_bit(pos->bb[WR],to)?WR:get_bit(pos->bb[WN],to)?WN:get_bit(pos->bb[WB],to)?WB:get_bit(pos->bb[WQ],to)?WQ:WK;
                }
                add_move(ml,s,to,BR,cap,-1,cap!=-1?MF_CAPTURE:MF_QUIET);
            }
        }
    }

    // Queens
    if (us==WHITE){
        for (U64 r=pos->bb[WQ]; r; r&=r-1){
            int s=LSB_INDEX(r); U64 moves=queen_attacks_from(1ULL<<s,pos->occ_all)&~own;
            for (U64 m=moves; m; m&=m-1){ int to=LSB_INDEX(m); int cap=-1;
                if (get_bit(opp,to)){
                    cap = get_bit(pos->bb[BP],to)?BP:get_bit(pos->bb[BR],to)?BR:get_bit(pos->bb[BN],to)?BN:get_bit(pos->bb[BB],to)?BB:get_bit(pos->bb[BQ],to)?BQ:BK;
                }
                add_move(ml,s,to,WQ,cap,-1,cap!=-1?MF_CAPTURE:MF_QUIET);
            }
        }
    } else {
        for (U64 r=pos->bb[BQ]; r; r&=r-1){
            int s=LSB_INDEX(r); U64 moves=queen_attacks_from(1ULL<<s,pos->occ_all)&~own;
            for (U64 m=moves; m; m&=m-1){ int to=LSB_INDEX(m); int cap=-1;
                if (get_bit(opp,to)){
                    cap = get_bit(pos->bb[WP],to)?WP:get_bit(pos->bb[WR],to)?WR:get_bit(pos->bb[WN],to)?WN:get_bit(pos->bb[WB],to)?WB:get_bit(pos->bb[WQ],to)?WQ:WK;
                }
                add_move(ml,s,to,BQ,cap,-1,cap!=-1?MF_CAPTURE:MF_QUIET);
            }
        }
    }

    // King + castling
    if (us==WHITE){
        int s = LSB_INDEX(pos->bb[WK]);
        U64 moves=king_attacks_from(1ULL<<s)&~own;
        for (U64 m=moves; m; m&=m-1){ int to=LSB_INDEX(m); int cap=-1;
            if (get_bit(opp,to)){
                cap = get_bit(pos->bb[BP],to)?BP:get_bit(pos->bb[BR],to)?BR:get_bit(pos->bb[BN],to)?BN:get_bit(pos->bb[BB],to)?BB:get_bit(pos->bb[BQ],to)?BQ:BK;
            }
            add_move(ml,s,to,WK,cap,-1,cap!=-1?MF_CAPTURE:MF_QUIET);
        }
        if (pos->castling & 1){ // K
            if (!(pos->occ_all & ((1ULL<<61)|(1ULL<<62)))){
                if (!square_attacked(pos,60,BLACK)&&!square_attacked(pos,61,BLACK)&&!square_attacked(pos,62,BLACK))
                    add_move(ml,60,62,WK,-1,-1,MF_CASTLE);
            }
        }
        if (pos->castling & 2){ // Q
            if (!(pos->occ_all & ((1ULL<<57)|(1ULL<<58)|(1ULL<<59)))){
                if (!square_attacked(pos,60,BLACK)&&!square_attacked(pos,59,BLACK)&&!square_attacked(pos,58,BLACK))
                    add_move(ml,60,58,WK,-1,-1,MF_CASTLE);
            }
        }
    } else {
        int s = LSB_INDEX(pos->bb[BK]);
        U64 moves=king_attacks_from(1ULL<<s)&~own;
        for (U64 m=moves; m; m&=m-1){ int to=LSB_INDEX(m); int cap=-1;
            if (get_bit(opp,to)){
                cap = get_bit(pos->bb[WP],to)?WP:get_bit(pos->bb[WR],to)?WR:get_bit(pos->bb[WN],to)?WN:get_bit(pos->bb[WB],to)?WB:get_bit(pos->bb[WQ],to)?WQ:WK;
            }
            add_move(ml,s,to,BK,cap,-1,cap!=-1?MF_CAPTURE:MF_QUIET);
        }
        if (pos->castling & 4){ // k
            if (!(pos->occ_all & ((1ULL<<5)|(1ULL<<6)))){
                if (!square_attacked(pos,4,WHITE)&&!square_attacked(pos,5,WHITE)&&!square_attacked(pos,6,WHITE))
                    add_move(ml,4,6,BK,-1,-1,MF_CASTLE);
            }
        }
        if (pos->castling & 8){ // q
            if (!(pos->occ_all & ((1ULL<<1)|(1ULL<<2)|(1ULL<<3)))){
                if (!square_attacked(pos,4,WHITE)&&!square_attacked(pos,3,WHITE)&&!square_attacked(pos,2,WHITE))
                    add_move(ml,4,2,BK,-1,-1,MF_CASTLE);
            }
        }
    }
}

// Make/Unmake
typedef struct { int castling, ep_square, halfmove_clock, fullmove_number, captured; } Undo;

static inline void move_piece_bb(Position *pos,int piece,int from,int to){ pop_bit(&pos->bb[piece],from); set_bit(&pos->bb[piece],to); }

static void make_move(Position *pos, const Move m, Undo *u){
    u->castling=pos->castling; u->ep_square=pos->ep_square; u->halfmove_clock=pos->halfmove_clock;
    u->fullmove_number=pos->fullmove_number; u->captured=m.captured;

    pos->halfmove_clock = (type_of_piece(m.piece)==0 || (m.flags&(MF_CAPTURE|MF_ENPASSANT))) ? 0 : pos->halfmove_clock+1;
    pos->ep_square = -1;

    if (m.flags & MF_ENPASSANT){
        if (pos->side==WHITE) pop_bit(&pos->bb[BP], m.to+8);
        else                  pop_bit(&pos->bb[WP], m.to-8);
    } else if (m.captured!=-1){
        pop_bit(&pos->bb[m.captured], m.to);
    }

    move_piece_bb(pos, m.piece, m.from, m.to);

    if (m.flags & MF_PROMO){
        if (pos->side==WHITE){ pop_bit(&pos->bb[WP], m.to); set_bit(&pos->bb[m.promo], m.to); }
        else                  { pop_bit(&pos->bb[BP], m.to); set_bit(&pos->bb[m.promo], m.to); }
    }

    if (m.flags & MF_CASTLE){
        if (m.piece==WK){
            if (m.to==62){ pop_bit(&pos->bb[WR],63); set_bit(&pos->bb[WR],61); }
            else if (m.to==58){ pop_bit(&pos->bb[WR],56); set_bit(&pos->bb[WR],59); }
        } else if (m.piece==BK){
            if (m.to==6){ pop_bit(&pos->bb[BR],7); set_bit(&pos->bb[BR],5); }
            else if (m.to==2){ pop_bit(&pos->bb[BR],0); set_bit(&pos->bb[BR],3); }
        }
    }

    // Update castling rights
    if (get_bit((1ULL<<m.from)|(1ULL<<m.to), 60) || m.piece==WK) pos->castling &= ~(1|2);
    if (get_bit(1ULL<<m.from, 63) || (m.captured==WR && m.to==63)) pos->castling &= ~1;
    if (get_bit(1ULL<<m.from, 56) || (m.captured==WR && m.to==56)) pos->castling &= ~2;
    if (get_bit((1ULL<<m.from)|(1ULL<<m.to), 4)  || m.piece==BK) pos->castling &= ~(4|8);
    if (get_bit(1ULL<<m.from, 7)  || (m.captured==BR && m.to==7))  pos->castling &= ~4;
    if (get_bit(1ULL<<m.from, 0)  || (m.captured==BR && m.to==0))  pos->castling &= ~8;

    if (m.flags & MF_DOUBLE) pos->ep_square = (pos->side==WHITE) ? (m.to+8) : (m.to-8);

    if (pos->side==BLACK) pos->fullmove_number++;
    pos->side = (Side)!pos->side;
    update_occupancy(pos);
}

static void unmake_move(Position *pos, const Move m, const Undo *u){
    pos->castling=u->castling; pos->ep_square=u->ep_square; pos->halfmove_clock=u->halfmove_clock; pos->fullmove_number=u->fullmove_number;
    pos->side = (Side)!pos->side;

    pop_bit(&pos->bb[m.piece], m.to); set_bit(&pos->bb[m.piece], m.from);

    if (m.flags & MF_PROMO){
        if (pos->side==WHITE){ pop_bit(&pos->bb[m.promo], m.to); set_bit(&pos->bb[WP], m.from); }
        else                  { pop_bit(&pos->bb[m.promo], m.to); set_bit(&pos->bb[BP], m.from); }
    }

    if (m.flags & MF_CASTLE){
        if (m.piece==WK){
            if (m.to==62){ pop_bit(&pos->bb[WR],61); set_bit(&pos->bb[WR],63); }
            else if (m.to==58){ pop_bit(&pos->bb[WR],59); set_bit(&pos->bb[WR],56); }
        } else if (m.piece==BK){
            if (m.to==6){ pop_bit(&pos->bb[BR],5); set_bit(&pos->bb[BR],7); }
            else if (m.to==2){ pop_bit(&pos->bb[BR],3); set_bit(&pos->bb[BR],0); }
        }
    }

    if (m.flags & MF_ENPASSANT){
        if (pos->side==WHITE) set_bit(&pos->bb[BP], m.to+8);
        else                  set_bit(&pos->bb[WP], m.to-8);
    } else if (u->captured!=-1){
        set_bit(&pos->bb[u->captured], m.to);
    }

    update_occupancy(pos);
}

// Legal movegen (filter checks)
static void generate_legal(const Position *pos, MoveList *out){
    MoveList ml; generate_pseudo_legal(pos,&ml);
    out->count=0;
    for (int i=0;i<ml.count;i++){
        Position p=*pos; Undo u; make_move(&p, ml.list[i], &u);
        if (p.side==WHITE){ // opponent to move
            int ks = LSB_INDEX(p.bb[BK]);
            if (!square_attacked(&p, ks, WHITE)) out->list[out->count++] = ml.list[i];
        } else {
            int ks = LSB_INDEX(p.bb[WK]);
            if (!square_attacked(&p, ks, BLACK)) out->list[out->count++] = ml.list[i];
        }
    }
}

// FEN
static void clear_position(Position *pos){
    memset(pos, 0, sizeof(*pos));
    pos->ep_square=-1; pos->halfmove_clock=0; pos->fullmove_number=1; pos->side=WHITE; pos->castling=0;
}

static bool set_from_fen(Position *pos, const char *fen){
    clear_position(pos);
    int file=0, rank=7;
    const char *p = fen;

    // Piece placement
    while (*p && rank>=0){
        if (*p==' ') break;
        if (*p=='/'){ if (file!=8) return false; file=0; rank--; p++; continue; }
        if (isdigit((unsigned char)*p)){
            int n=*p-'0'; if (n<1 || n>8) return false;
            file += n; if (file>8) return false; p++; continue;
        }
        int piece=-1;
        switch(*p){
            case 'P': piece=WP; break; case 'R': piece=WR; break; case 'N': piece=WN; break; case 'B': piece=WB; break; case 'Q': piece=WQ; break; case 'K': piece=WK; break;
            case 'p': piece=BP; break; case 'r': piece=BR; break; case 'n': piece=BN; break; case 'b': piece=BB; break; case 'q': piece=BQ; break; case 'k': piece=BK; break;
            default: return false;
        }
        if (file>=8) return false;
        int sq = (7-rank)*8 + file;
        set_bit(&pos->bb[piece], sq);
        file++; p++;
        if (file==8 && rank>0 && *p!='/' && *p!=' ') return false;
    }
    if (rank!=0 || file!=8) return false;
    while (*p==' ') p++;

    // Side
    if (*p=='w') pos->side=WHITE;
    else if (*p=='b') pos->side=BLACK;
    else return false;
    p++;
    if (*p != ' ') { return false; }
    p++;

    // Castling
    pos->castling=0;
    if (*p=='-'){ p++; }
    else {
        while (*p && *p!=' '){
            if (*p=='K') pos->castling|=1;
            else if (*p=='Q') pos->castling|=2;
            else if (*p=='k') pos->castling|=4;
            else if (*p=='q') pos->castling|=8;
            else return false;
            p++;
        }
    }
    if (*p!=' ') return false;
    p++;

    // En passant
    if (*p=='-'){ pos->ep_square=-1; p++; }
    else {
        if (p[0]>='a'&&p[0]<='h'&&p[1]>='1'&&p[1]<='8'){
            int f=p[0]-'a', r=p[1]-'1';
            pos->ep_square = r*8+f;
            p+=2;
        } else return false;
    }
    if (*p==' ') p++;

    // Halfmove & fullmove (optional)
    if (*p){ pos->halfmove_clock = atoi(p); while(*p&&*p!=' ') p++; if (*p==' ') p++; }
    if (*p){ pos->fullmove_number= atoi(p); }

    update_occupancy(pos);
    return true;
}

static const char *FEN_STARTPOS="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Perft
static uint64_t perft(Position *pos, int depth){
    if (depth==0) return 1ULL;
    MoveList ml; generate_legal(pos,&ml);
    uint64_t nodes=0;
    for (int i=0;i<ml.count;i++){
        Undo u; make_move(pos, ml.list[i], &u);
        nodes += perft(pos, depth-1);
        unmake_move(pos, ml.list[i], &u);
    }
    return nodes;
}

// Eval/Search
static const int PIECE_VAL[6]={100,500,320,330,900,20000};

static int evaluate(const Position *pos){
    int s=0;
    s += popcount64(pos->bb[WP])*PIECE_VAL[0] + popcount64(pos->bb[WR])*PIECE_VAL[1]
       + popcount64(pos->bb[WN])*PIECE_VAL[2] + popcount64(pos->bb[WB])*PIECE_VAL[3]
       + popcount64(pos->bb[WQ])*PIECE_VAL[4];
    s -= popcount64(pos->bb[BP])*PIECE_VAL[0] + popcount64(pos->bb[BR])*PIECE_VAL[1]
       + popcount64(pos->bb[BN])*PIECE_VAL[2] + popcount64(pos->bb[BB])*PIECE_VAL[3]
       + popcount64(pos->bb[BQ])*PIECE_VAL[4];
    return (pos->side==WHITE) ? s : -s;
}

static int alphabeta(Position *pos,int depth,int alpha,int beta){
    if (depth==0) return evaluate(pos);
    MoveList ml; generate_legal(pos,&ml);
    if (ml.count==0){
        int ks = (pos->side==WHITE)? LSB_INDEX(pos->bb[WK]) : LSB_INDEX(pos->bb[BK]);
        if (square_attacked(pos, ks, (Side)!pos->side)) return -20000 + (5-depth);
        return 0; // stalemate
    }
    for (int i=0;i<ml.count;i++){
        Undo u; make_move(pos, ml.list[i], &u);
        int score = -alphabeta(pos, depth-1, -beta, -alpha);
        unmake_move(pos, ml.list[i], &u);
        if (score>=beta) return beta;
        if (score>alpha) alpha=score;
    }
    return alpha;
}

static Move search(Position *pos,int depth){
    Move best={0}; best.from=-1; int bestScore=-30000;
    MoveList ml; generate_legal(pos,&ml);
    for (int i=0;i<ml.count;i++){
        Undo u; make_move(pos, ml.list[i], &u);
        int score = -alphabeta(pos, depth-1, -30000, 30000);
        unmake_move(pos, ml.list[i], &u);
        if (score>bestScore){ bestScore=score; best=ml.list[i]; }
    }
    return best;
}

// Printing
static void print_board(const Position *pos){
    printf("\n");
    for (int r=8;r>=1;--r){
        printf("%d ", r);
        for (int f=0;f<8;++f){
            int sq=(8-r)*8+f; char c='.';
            for (int p=0;p<PIECE_N;++p) if (get_bit(pos->bb[p],sq)){ c=PIECE_CHARS[p]; break; }
            printf("%c ", c);
        }
        printf("\n");
    }
    printf("  a b c d e f g h\n\n");
}
static const char* sq_to_str(int sq){ static char b[3]; b[0]='a'+(sq%8); b[1]='1'+(sq/8); b[2]=0; return b; }
static void print_move(const Move m){
    if (m.flags & MF_PROMO){
        int t = type_of_piece(m.promo);
        char pr = (t==4)?'q':(t==1)?'r':(t==3)?'b':'n';
        printf("%s%s%c", sq_to_str(m.from), sq_to_str(m.to), pr);
    } else {
        printf("%s%s", sq_to_str(m.from), sq_to_str(m.to));
    }
}

// UCI loop (minimal)
static void uci_loop(void){
    char line[4096];
    Position pos; set_from_fen(&pos, FEN_STARTPOS);
    printf("id name ffp\nid author you\nuciok\n"); fflush(stdout);
    while (fgets(line,sizeof(line),stdin)){
        if      (!strncmp(line,"uci",3))      { printf("id name ffp\nid author you\nuciok\n"); fflush(stdout); }
        else if (!strncmp(line,"isready",7))  { printf("readyok\n"); fflush(stdout); }
        else if (!strncmp(line,"ucinewgame",10)){ set_from_fen(&pos, FEN_STARTPOS); }
        else if (!strncmp(line,"position",8)){
            char *ptr=line+8; while(*ptr==' ') ptr++;
            if (!strncmp(ptr,"startpos",8)){ set_from_fen(&pos, FEN_STARTPOS); ptr+=8; }
            else if (!strncmp(ptr,"fen",3)){
                ptr+=3; while(*ptr==' ') ptr++;
                char fen[256]={0}; int fi=0, spaces=0;
                while (*ptr && fi<255){
                    if (*ptr=='\n') break;
                    fen[fi++]=*ptr;
                    if (*ptr==' ') spaces++;
                    if (spaces==5) break; // piece/side/castling/ep/halfmove (stop before fullmove)
                    ptr++;
                }
                fen[fi]=0; set_from_fen(&pos, fen);
            }
            char *mstr = strstr(ptr, "moves");
            if (mstr){
                mstr+=5;
                while (*mstr){
                    while (*mstr==' ') mstr++;
                    if (!*mstr || *mstr=='\n') break;
                    int ffile=mstr[0]-'a', frank=mstr[1]-'1';
                    int tfile=mstr[2]-'a', trank=mstr[3]-'1';
                    int from=frank*8+ffile, to=trank*8+tfile, promo=-1;
                    if (mstr[4] && mstr[4]!=' ' && mstr[4]!='\n'){
                        char pc=tolower((unsigned char)mstr[4]);
                        promo = (pc=='q')? ((pos.side==WHITE)?WQ:BQ)
                              : (pc=='r')? ((pos.side==WHITE)?WR:BR)
                              : (pc=='b')? ((pos.side==WHITE)?WB:BB)
                              : (pc=='n')? ((pos.side==WHITE)?WN:BN) : -1;
                    }
                    MoveList legal; generate_legal(&pos,&legal);
                    for (int i=0;i<legal.count;i++){
                        Move mv=legal.list[i];
                        if (mv.from==from && mv.to==to){
                            if ((promo==-1 && !(mv.flags&MF_PROMO)) || (promo!=-1 && (mv.flags&MF_PROMO) && mv.promo==promo)){
                                Undo u; make_move(&pos, mv, &u); break;
                            }
                        }
                    }
                    while (*mstr && *mstr!=' ') mstr++;
                }
            }
        }
        else if (!strncmp(line,"go",2)){
            int depth=4; char *dpos=strstr(line,"depth"); if (dpos){ depth=atoi(dpos+5); if (depth<=0) depth=4; }
            Move best=search(&pos, depth);
            printf("bestmove "); print_move(best); printf("\n"); fflush(stdout);
        }
        else if (!strncmp(line,"d",1)) { print_board(&pos); fflush(stdout); }
        else if (!strncmp(line,"perft",5)){
            int depth=atoi(line+5); uint64_t nodes=perft(&pos, depth);
            printf("nodes %llu\n", (unsigned long long)nodes); fflush(stdout);
        }
        else if (!strncmp(line,"quit",4)) break;
    }
}

// CLI
static void usage(void){
    printf("ffp — single-file chess engine\n");
    printf("Usage:\n");
    printf("  ./ffp                 # show start position and a sample search\n");
    printf("  ./ffp --fen \"<FEN>\"   # load FEN and print board\n");
    printf("  ./ffp --perft N       # perft to depth N\n");
    printf("  ./ffp --search N      # search depth N and print best move\n");
    printf("  ./ffp --uci           # start minimal UCI loop\n\n");
}

int main(int argc,char **argv){
    Position pos; set_from_fen(&pos, FEN_STARTPOS);
    if (argc==1){
        print_board(&pos);
        Move best=search(&pos,4);
        printf("Suggest: "); print_move(best); printf(" (depth 4)\n");
        return 0;
    }
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--help")) { usage(); return 0; }
        else if (!strcmp(argv[i],"--uci")) { uci_loop(); return 0; }
        else if (!strcmp(argv[i],"--fen") && i+1<argc) { set_from_fen(&pos, argv[++i]); }
        else if (!strcmp(argv[i],"--perft") && i+1<argc){
            int depth=atoi(argv[++i]);
            clock_t t0=clock(); uint64_t nodes=perft(&pos, depth);
            double sec=(double)(clock()-t0)/CLOCKS_PER_SEC;
            printf("perft(%d) = %llu  (%.3fs, %.0f kn/s)\n", depth, (unsigned long long)nodes, sec, sec>0?(nodes/1000.0/sec):0);
            return 0;
        }
        else if (!strcmp(argv[i],"--search") && i+1<argc){
            int depth=atoi(argv[++i]); Move best=search(&pos, depth);
            printf("best move: "); print_move(best); printf("\n"); return 0;
        }
        else { usage(); return 1; }
    }
    print_board(&pos);
    return 0;
}
