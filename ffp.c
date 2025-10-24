// ffp.c - Far-From-Perfect chess engine (single file)
// Features: bitboards, FEN loader, pseudo-legal + legal movegen (incl. EP/castling/promotions),
// make/unmake with undo, perft, simple alpha-beta (material only), minimal UCI loop.
//
// Board indexing: a8 = 0, ..., h8 = 7, a7 = 8, ..., a1 = 56, ..., h1 = 63.
//
// Build:  gcc -O2 -Wall -Wextra -o ffp ffp.c
// Run:    ./ffp --help   |   ./ffp --uci   |   ./ffp --perft 4

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "ffp.h"

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

// Piece chars
static const char PIECE_CHARS[PIECE_N] = {'P','R','N','B','Q','K','p','r','n','b','q','k'};
static inline int type_of_piece(int p){ return p % 6; }

// Default FEN string (start position)
static const char FEN_STARTPOS[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

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

static inline void update_occupancy(Position *pos){
    pos->occ_white = pos->bb[WP]|pos->bb[WR]|pos->bb[WN]|pos->bb[WB]|pos->bb[WQ]|pos->bb[WK];
    pos->occ_black = pos->bb[BP]|pos->bb[BR]|pos->bb[BN]|pos->bb[BB]|pos->bb[BQ]|pos->bb[BK];
    pos->occ_all   = pos->occ_white | pos->occ_black;
}

// Forward declaration for FEN loader
static bool set_from_fen(Position *pos, const char *fen);

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
bool ffp_is_square_attacked(const Position *pos, int sq, Side by){
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
static inline void add_move(MoveList *ml,int from,int to,int piece,int captured,int promo,int flags){
    ml->list[ml->count++] = (Move){from,to,piece,promo,captured,flags};
}

// Pseudo-legal generator
void ffp_generate_pseudo_legal(const Position *pos, MoveList *ml){
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
                if (!ffp_is_square_attacked(pos,60,BLACK)&&!ffp_is_square_attacked(pos,61,BLACK)&&!ffp_is_square_attacked(pos,62,BLACK))
                    add_move(ml,60,62,WK,-1,-1,MF_CASTLE);
            }
        }
        if (pos->castling & 2){ // Q
            if (!(pos->occ_all & ((1ULL<<57)|(1ULL<<58)|(1ULL<<59)))){
                if (!ffp_is_square_attacked(pos,60,BLACK)&&!ffp_is_square_attacked(pos,59,BLACK)&&!ffp_is_square_attacked(pos,58,BLACK))
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
                if (!ffp_is_square_attacked(pos,4,WHITE)&&!ffp_is_square_attacked(pos,5,WHITE)&&!ffp_is_square_attacked(pos,6,WHITE))
                    add_move(ml,4,6,BK,-1,-1,MF_CASTLE);
            }
        }
        if (pos->castling & 8){ // q
            if (!(pos->occ_all & ((1ULL<<1)|(1ULL<<2)|(1ULL<<3)))){
                if (!ffp_is_square_attacked(pos,4,WHITE)&&!ffp_is_square_attacked(pos,3,WHITE)&&!ffp_is_square_attacked(pos,2,WHITE))
                    add_move(ml,4,2,BK,-1,-1,MF_CASTLE);
            }
        }
    }
}

static inline void move_piece_bb(Position *pos,int piece,int from,int to){ pop_bit(&pos->bb[piece],from); set_bit(&pos->bb[piece],to); }

void ffp_make_move(Position *pos, const Move m, Undo *u){
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

void ffp_unmake_move(Position *pos, const Move m, const Undo *u){
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
void ffp_generate_legal(const Position *pos, MoveList *out){
    MoveList ml; ffp_generate_pseudo_legal(pos,&ml);
    out->count=0;
    for (int i=0;i<ml.count;i++){
        Position p=*pos; Undo u; ffp_make_move(&p, ml.list[i], &u);
        if (p.side==WHITE){ // opponent to move
            int ks = LSB_INDEX(p.bb[BK]);
            if (!ffp_is_square_attacked(&p, ks, WHITE)) out->list[out->count++] = ml.list[i];
        } else {
            int ks = LSB_INDEX(p.bb[WK]);
            if (!ffp_is_square_attacked(&p, ks, BLACK)) out->list[out->count++] = ml.list[i];
        }
    }
}

int ffp_generate_legal_array(const Position *pos, Move *moves, int max_moves){
    MoveList ml; ffp_generate_legal(pos, &ml);
    if (moves && max_moves>0){
        int copy = ml.count < max_moves ? ml.count : max_moves;
        memcpy(moves, ml.list, sizeof(Move)*copy);
    }
    return ml.count;
}

// FEN
void ffp_position_clear(Position *pos){
    memset(pos, 0, sizeof(*pos));
    pos->ep_square=-1;
    pos->halfmove_clock=0;
    pos->fullmove_number=1;
    pos->side=WHITE;
    pos->castling=0;
}

bool ffp_position_from_fen(Position *pos, const char *fen){
    ffp_position_clear(pos);
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

const char *FFP_FEN_STARTPOS="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

void ffp_position_set_start(Position *pos){
    ffp_position_from_fen(pos, FFP_FEN_STARTPOS);
}

bool ffp_position_to_fen(const Position *pos, char *buffer, size_t length){
    if (!buffer || length==0) return false;
    char temp[128];
    size_t idx=0;
    for (int rank=7; rank>=0; --rank){
        int empty=0;
        for (int file=0; file<8; ++file){
            int sq=rank*8+file;
            int piece=-1;
            for (int p=0; p<PIECE_N; ++p){
                if (get_bit(pos->bb[p], sq)){ piece=p; break; }
            }
            if (piece==-1){
                empty++;
            } else {
                if (empty>0){ temp[idx++] = '0' + empty; empty=0; }
                temp[idx++] = PIECE_CHARS[piece];
            }
        }
        if (empty>0) temp[idx++] = '0' + empty;
        if (rank>0) temp[idx++] = '/';
    }
    temp[idx++] = ' ';
    temp[idx++] = (pos->side==WHITE)?'w':'b';
    temp[idx++] = ' ';
    if (pos->castling==0){
        temp[idx++]='-';
    } else {
        if (pos->castling & 1) temp[idx++]='K';
        if (pos->castling & 2) temp[idx++]='Q';
        if (pos->castling & 4) temp[idx++]='k';
        if (pos->castling & 8) temp[idx++]='q';
    }
    temp[idx++]=' ';
    if (pos->ep_square==-1){
        temp[idx++]='-';
    } else {
        temp[idx++]='a'+(pos->ep_square%8);
        temp[idx++]='1'+(pos->ep_square/8);
    }
    temp[idx++]=' ';
    idx += snprintf(temp+idx, sizeof(temp)-idx, "%d %d", pos->halfmove_clock, pos->fullmove_number);
    if (idx+1>sizeof(temp)) return false;
    temp[idx]='\0';
    if (strlen(temp)+1>length) return false;
    strcpy(buffer, temp);
    return true;
}

// Perft
static uint64_t perft(Position *pos, int depth){
    if (depth==0) return 1ULL;
    MoveList ml; ffp_generate_legal(pos,&ml);
    uint64_t nodes=0;
    for (int i=0;i<ml.count;i++){
        Undo u; ffp_make_move(pos, ml.list[i], &u);
        nodes += perft(pos, depth-1);
        ffp_unmake_move(pos, ml.list[i], &u);
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

typedef struct {
    uint64_t nodes;
    clock_t start;
    SearchLimits limits;
    bool aborted;
} SearchContext;

static bool search_should_abort(SearchContext *ctx){
    if (ctx->aborted) return true;
    if (ctx->limits.node_limit && ctx->nodes >= ctx->limits.node_limit){ ctx->aborted = true; return true; }
    if (ctx->limits.stop && *ctx->limits.stop){ ctx->aborted = true; return true; }
    if (ctx->limits.time_ms > 0){
        double elapsed_ms = (double)(clock() - ctx->start) * 1000.0 / CLOCKS_PER_SEC;
        if (elapsed_ms >= ctx->limits.time_ms){ ctx->aborted = true; return true; }
    }
    return false;
}

static int alphabeta(Position *pos,int depth,int alpha,int beta, SearchContext *ctx){
    if (search_should_abort(ctx)) return 0;
    ctx->nodes++;
    if (depth==0) return evaluate(pos);
    MoveList ml; ffp_generate_legal(pos,&ml);
    if (ml.count==0){
        int ks = (pos->side==WHITE)? LSB_INDEX(pos->bb[WK]) : LSB_INDEX(pos->bb[BK]);
        if (ffp_is_square_attacked(pos, ks, (Side)!pos->side)) return -20000 + (5-depth);
        return 0; // stalemate
    }
    for (int i=0;i<ml.count;i++){
        Undo u; ffp_make_move(pos, ml.list[i], &u);
        int score = -alphabeta(pos, depth-1, -beta, -alpha, ctx);
        ffp_unmake_move(pos, ml.list[i], &u);
        if (ctx->aborted) return 0;
        if (score>=beta) return beta;
        if (score>alpha) alpha=score;
    }
    return alpha;
}

SearchResult ffp_search(Position *pos, const SearchLimits *limits){
    SearchLimits effective = {0};
    if (limits) effective = *limits;
    if (effective.max_depth <= 0) effective.max_depth = 4;

    SearchContext ctx = {0};
    ctx.nodes = 0;
    ctx.start = clock();
    ctx.limits = effective;
    ctx.aborted = false;

    SearchResult result = {0};
    result.best_move.from = -1;
    result.best_move.to = -1;
    result.best_move.piece = -1;
    result.best_move.promo = -1;
    result.best_move.captured = -1;
    result.best_move.flags = 0;
    result.depth_reached = 0;
    result.score = 0;
    result.nodes = 0;
    result.aborted = false;

    MoveList rootMoves;
    ffp_generate_legal(pos, &rootMoves);
    if (rootMoves.count==0){
        int ks = (pos->side==WHITE)? LSB_INDEX(pos->bb[WK]) : LSB_INDEX(pos->bb[BK]);
        bool in_check = ffp_is_square_attacked(pos, ks, (Side)!pos->side);
        result.score = in_check ? -20000 : 0;
        result.aborted = false;
        return result;
    }

    Move best_so_far = rootMoves.list[0];
    int max_depth = effective.max_depth;
    for (int depth=1; depth<=max_depth; ++depth){
        int best_score=-30000;
        Move best_move_depth = rootMoves.list[0];
        bool found=false;

        for (int i=0;i<rootMoves.count;i++){
            if (search_should_abort(&ctx)) break;
            Undo u; ffp_make_move(pos, rootMoves.list[i], &u);
            int score = -alphabeta(pos, depth-1, -30000, 30000, &ctx);
            ffp_unmake_move(pos, rootMoves.list[i], &u);
            if (ctx.aborted) break;
            if (!found || score>best_score){
                best_score=score;
                best_move_depth=rootMoves.list[i];
                found=true;
            }
        }

        result.nodes = ctx.nodes;
        result.aborted = ctx.aborted;
        if (ctx.aborted) break;
        if (found){
            best_so_far = best_move_depth;
            result.best_move = best_so_far;
            result.depth_reached = depth;
            result.score = best_score;
        }
    }

    if (result.best_move.from==-1){
        result.best_move = best_so_far;
    }
    result.nodes = ctx.nodes;
    result.aborted = ctx.aborted;
    return result;
}

void ffp_move_to_string(const Move *move, char out[6]){
    if (!out) return;
    if (!move || move->from < 0 || move->to < 0){
        out[0]='\0';
        return;
    }
    out[0]='a'+(move->from%8);
    out[1]='1'+(move->from/8);
    out[2]='a'+(move->to%8);
    out[3]='1'+(move->to/8);
    if (move->flags & MF_PROMO){
        int t = type_of_piece(move->promo);
        out[4] = (t==4)?'q':(t==1)?'r':(t==3)?'b':'n';
        out[5]='\0';
    } else {
        out[4]='\0';
    }
}

bool ffp_move_from_string(const Position *pos, const char *uci, Move *out_move){
    if (!pos || !uci || strlen(uci) < 4) return false;
    int ffile = uci[0]-'a';
    int frank = uci[1]-'1';
    int tfile = uci[2]-'a';
    int trank = uci[3]-'1';
    if (ffile<0||ffile>7||tfile<0||tfile>7||frank<0||frank>7||trank<0||trank>7) return false;
    int from = frank*8 + ffile;
    int to = trank*8 + tfile;
    int promo = -1;
    if (uci[4]){
        char pc = tolower((unsigned char)uci[4]);
        promo = (pc=='q')? ((pos->side==WHITE)?WQ:BQ)
              : (pc=='r')? ((pos->side==WHITE)?WR:BR)
              : (pc=='b')? ((pos->side==WHITE)?WB:BB)
              : (pc=='n')? ((pos->side==WHITE)?WN:BN) : -1;
    }
    MoveList legal; ffp_generate_legal(pos,&legal);
    for (int i=0;i<legal.count;i++){
        Move mv = legal.list[i];
        if (mv.from==from && mv.to==to){
            if ((promo==-1 && !(mv.flags&MF_PROMO)) || (promo!=-1 && (mv.flags&MF_PROMO) && mv.promo==promo)){
                if (out_move) *out_move = mv;
                return true;
            }
        }
    }
    return false;
}

// Printing
void ffp_print_board(const Position *pos){
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
static void print_move(const Move m){
    char buf[6];
    ffp_move_to_string(&m, buf);
    printf("%s", buf);
}

// UCI loop (minimal)
static void uci_loop(void){
    char line[4096];
    Position pos; ffp_position_set_start(&pos);
    printf("id name ffp\nid author you\nuciok\n"); fflush(stdout);
    while (fgets(line,sizeof(line),stdin)){
        if      (!strncmp(line,"uci",3))      { printf("id name ffp\nid author you\nuciok\n"); fflush(stdout); }
        else if (!strncmp(line,"isready",7))  { printf("readyok\n"); fflush(stdout); }
        else if (!strncmp(line,"ucinewgame",10)){ ffp_position_set_start(&pos); }
        else if (!strncmp(line,"position",8)){
            char *ptr=line+8; while(*ptr==' ') ptr++;
            if (!strncmp(ptr,"startpos",8)){ ffp_position_set_start(&pos); ptr+=8; }
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
                fen[fi]=0; ffp_position_from_fen(&pos, fen);
            }
            char *mstr = strstr(ptr, "moves");
            if (mstr){
                mstr+=5;
                while (*mstr){
                    while (*mstr==' ') mstr++;
                    if (!*mstr || *mstr=='\n') break;
                    char mvbuf[6]={0};
                    int len=0;
                    while (mstr[len] && mstr[len]!=' ' && mstr[len]!='\n' && len<5){
                        mvbuf[len]=mstr[len];
                        len++;
                    }
                    mvbuf[len]='\0';
                    Move mv;
                    if (ffp_move_from_string(&pos, mvbuf, &mv)){
                        Undo u; ffp_make_move(&pos, mv, &u);
                    }
                    while (mstr[len] && mstr[len]!=' ') len++;
                    mstr += len;
                }
            }
        }
        else if (!strncmp(line,"go",2)){
            SearchLimits limits = {0};
            char *dpos=strstr(line,"depth");
            if (dpos){
                int depth=atoi(dpos+5);
                if (depth>0) limits.max_depth=depth;
            }
            char *tpos=strstr(line,"movetime");
            if (tpos){
                int ms=atoi(tpos+8);
                if (ms>0) limits.time_ms=ms;
            }
            char *npos=strstr(line,"nodes");
            if (npos){
                unsigned long long nodes=strtoull(npos+5, NULL, 10);
                limits.node_limit = nodes;
            }
            SearchResult res = ffp_search(&pos, &limits);
            char buf[6];
            ffp_move_to_string(&res.best_move, buf);
            if (buf[0]==0) strcpy(buf, "0000");
            printf("bestmove %s\n", buf); fflush(stdout);
        }
        else if (!strncmp(line,"d",1)) { ffp_print_board(&pos); fflush(stdout); }
        else if (!strncmp(line,"perft",5)){
            int depth=atoi(line+5); uint64_t nodes=perft(&pos, depth);
            printf("nodes %llu\n", (unsigned long long)nodes); fflush(stdout);
        }
        else if (!strncmp(line,"quit",4)) break;
    }
}

// CLI
static void usage(void){
    printf("ffp - for-from-perfect chess engine\n");
    printf("Usage:\n");
    printf("  ./ffp                  # show start position and a sample search\n");
    printf("  ./ffp --fen \"<FEN>\"  # load FEN and print board\n");
    printf("  ./ffp --perft N        # perft to depth N\n");
    printf("  ./ffp --search N       # search depth N and print best move\n");
    printf("  ./ffp --search-time MS # search with time limit in ms\n");
    printf("  ./ffp --uci            # start minimal UCI loop\n\n");
}

int main(int argc,char **argv){
    Position pos; set_from_fen(&pos, FEN_STARTPOS);
    if (argc==1){
        ffp_print_board(&pos);
        SearchLimits limits = {.max_depth=4};
        SearchResult res = ffp_search(&pos, &limits);
        Move best=res.best_move;
        printf("Suggest: "); print_move(best); printf(" (depth 4)\n");
        return 0;
    }
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--help")) { usage(); return 0; }
        else if (!strcmp(argv[i],"--uci")) { uci_loop(); return 0; }
        else if (!strcmp(argv[i],"--fen") && i+1<argc) { ffp_position_from_fen(&pos, argv[++i]); }
        else if (!strcmp(argv[i],"--perft") && i+1<argc){
            int depth=atoi(argv[++i]);
            clock_t t0=clock(); uint64_t nodes=perft(&pos, depth);
            double sec=(double)(clock()-t0)/CLOCKS_PER_SEC;
            printf("perft(%d) = %llu  (%.3fs, %.0f kn/s)\n", depth, (unsigned long long)nodes, sec, sec>0?(nodes/1000.0/sec):0);
            return 0;
        }
        else if (!strcmp(argv[i],"--search") && i+1<argc){
            int depth=atoi(argv[++i]);
            SearchLimits limits = {.max_depth = depth>0?depth:4};
            SearchResult res = ffp_search(&pos, &limits);
            Move best=res.best_move;
            printf("best move: "); print_move(best); printf("\n"); return 0;
        }
        else if (!strcmp(argv[i],"--search-time") && i+1<argc){
            int ms=atoi(argv[++i]);
            SearchLimits limits = {.time_ms = ms>0?ms:0};
            SearchResult res = ffp_search(&pos, &limits);
            Move best=res.best_move;
            printf("best move: "); print_move(best); printf("\n"); return 0;
        }
        else { usage(); return 1; }
    }
    ffp_print_board(&pos);
    return 0;
}
