#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

// define bitboard data type
#define U64 unsigned long long

#define get_bit(bb, sq) (bb & (1ULL << sq))
#define set_bit(bb, sq) (bb |= (1ULL << sq))
#define pop_bit(bb, sq) ((bb) &= ~(1ULL << (sq)))

// https://www.chessprogramming.org/Square_Mapping_Considerations
enum {
    a8, b8, c8, d8, e8, f8, g8, h8,
    a7, b7, c7, d7, e7, f7, g7, h7,
    a6, b6, c6, d6, e6, f6, g6, h6,
    a5, b5, c5, d5, e5, f5, g5, h5,
    a4, b4, c4, d4, e4, f4, g4, h4,
    a3, b3, c3, d3, e3, f3, g3, h3,
    a2, b2, c2, d2, e2, f2, g2, h2,
    a1, b1, c1, d1, e1, f1, g1, h1
};

enum {
    WP, WR, WN, WB, WQ, WK,
    BP, BR, BN, BB, BQ, BK
};

char characters[12] = {'P', 'R', 'N', 'B', 'Q', 'K', 'p', 'r', 'n', 'b', 'q', 'k'};

//a-file             0x0101010101010101
//h-file             0x8080808080808080
//1st rank           0x00000000000000FF
//8th rank           0xFF00000000000000
//a1-h8 diagonal     0x8040201008040201
//h1-a8 antidiagonal 0x0102040810204080
//light squares      0x55AA55AA55AA55AA
//dark squares       0xAA55AA55AA55AA55

// print a given bitboard
void print_bitboard(U64 bitboard) {
    printf("\n");
    for (int rank = 0; rank < 8; rank++) {
        printf("%d", 8 - rank);
        for (int file = 0; file < 8; file++) {
            int square = rank * 8 + file;

            //printf(" %d ", square); // for bitboard indexes
            printf(" %d ", get_bit(bitboard, square) ? 1 : 0); // for bitboard bits
        }
        printf("\n");
    }
    printf("  A  B  C  D  E  F  G  H\n\n");
}

// print the game board with respective pieces
void print_board(U64 bitboards[12], uint8_t side) {
    char board[64];

    for (int i = 0; i < 64; i++) {
        board[i] = '.';
    }

    for (int bb = 0; bb < 12; bb++) {
        U64 bitboard = bitboards[bb];

        for (int square = 0; square < 64; square++) {
            if (get_bit(bitboard, square)) {
                board[square] = characters[bb];
            }
        }
    }

    printf("\n\n");
    for (int rank = 0; rank < 8; rank++) {
        printf("%d", 8 - rank);
        for (int file = 0; file < 8; file++) {
            int square = rank * 8 + file;
            printf(" %c ", board[square]);
        }
        printf("\n");
    }
    printf("  A  B  C  D  E  F  G  H");
    printf("\n\n\n");
    (side) ? printf("White's turn\n") : printf("Black's turn\n");
}

// the set of occupied squares in a game
U64 get_occupied(U64 bitboards[12]) {
    U64 board = 0ULL;
    for (int bb = 0; bb < 12; bb++) board |= bitboards[bb];
    return board;
}

// the set of empty squares in a game
U64 get_empty_squares(U64 bitboards[12]) {
    return ~get_occupied(bitboards);
}

// ATTACKS
U64 get_pawn_attacks(uint8_t side, uint8_t square) {
    U64 bitboard = 0ULL;

    return bitboard;
}


int main() {
    U64 bitboards[12];
    bitboards[WP] = 0xff000000000000ULL; // white pawns
    bitboards[WR] = 0x8100000000000000ULL; // white rooks
    bitboards[WN] = 0x4200000000000000ULL; // white knights
    bitboards[WB] = 0x2400000000000000ULL; // white bishops
    bitboards[WQ] = 0x800000000000000ULL; // white queen
    bitboards[WK] = 0x1000000000000000ULL; // white king
    bitboards[BP] = 0xff00ULL; // black pawns
    bitboards[BR] = 0x81ULL; // black rooks
    bitboards[BN] = 0x42ULL; // black knights
    bitboards[BB] = 0x24ULL; // black bishops
    bitboards[BQ] = 0x8ULL; // black queen
    bitboards[BK] = 0x10ULL; // black king

    uint8_t side = 1; // white = 1, black = 2
    U64 white_pawn_attacks = get_pawn_attacks(side, d2); // white d2 pawn
    print_bitboard(white_pawn_attacks);

    return 0;
}
