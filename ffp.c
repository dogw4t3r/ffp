#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#define get_bit(b, s) (b & (1ULL << s))
#define set_bit(b, s) (b |= (1ULL << s))
#define pop_bit(b, s) ((b) &= ~(1ULL << (s)))

typedef uint64_t U64;

//a-file             0x0101010101010101
//h-file             0x8080808080808080
//1st rank           0x00000000000000FF
//8th rank           0xFF00000000000000
//a1-h8 diagonal     0x8040201008040201
//h1-a8 antidiagonal 0x0102040810204080
//light squares      0x55AA55AA55AA55AA
//dark squares       0xAA55AA55AA55AA55
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

// https://www.chessprogramming.org/General_Setwise_Operations#ShiftingBitboards
const U64 avoidWrap[8] =
{
   0xfefefefefefefe00, // not a-file and 1st rank
   0xfefefefefefefefe, // not a-file
   0x00fefefefefefefe, // not 1st rank
   0x00ffffffffffffff, // not 8th rank
   0x007f7f7f7f7f7f7f, // not h-file and 8th rank
   0x7f7f7f7f7f7f7f7f, // not h-file
   0x7f7f7f7f7f7f7f00, // not h-file and 1st rank
   0xffffffffffffff00, // not 1st rank
};

// SHIFT OPERATIONS
U64 shift_north(U64 b)      { return b >> 8; }
U64 shift_south(U64 b)      { return b << 8; }
U64 shift_east(U64 b)       { return (b << 1) & avoidWrap[1]; }
U64 shift_north_east(U64 b) { return (b >> 7) & avoidWrap[1]; }
U64 shift_south_east(U64 b) { return (b << 9) & avoidWrap[1]; }
U64 shift_west(U64 b)       { return (b >> 1) & avoidWrap[5]; }
U64 shift_north_west(U64 b) { return (b >> 9) & avoidWrap[5]; }
U64 shift_south_west(U64 b) { return (b << 7) & avoidWrap[5]; }

// PRINTING HELPER FUNCTIONS
void print_bitboard(U64 b) {
    printf("\n");
    for (int r = 0; r < 8; r++) {
        printf("%d", 8 - r);
        for (int f = 0; f < 8; f++) {
            int s = r * 8 + f;

            printf(" %d ", get_bit(b, s) ? 1 : 0);
        }
        printf("\n");
    }
    printf("  A  B  C  D  E  F  G  H\n\n");
}

void print_board(U64 bitboards[12], uint8_t side) {
    char board[64];

    for (int i = 0; i < 64; i++) {
        board[i] = '.';
    }

    for (int b = 0; b < 12; b++) {
        U64 bitboard = bitboards[b];

        for (int s = 0; s < 64; s++) {
            if (get_bit(bitboard, s)) {
                board[s] = characters[b];
            }
        }
    }

    printf("\n\n");
    for (int r = 0; r < 8; r++) {
        printf("%d", 8 - r);
        for (int f = 0; f < 8; f++) {
            int s = r * 8 + f;
            printf(" %c ", board[s]);
        }
        printf("\n");
    }
    printf("  A  B  C  D  E  F  G  H");
    printf("\n\n\n");
    (side) ? printf("White's turn\n") : printf("Black's turn\n");
}

// BITBOARD HELPERS
U64 get_occupied_squares(U64 bitboards[12]) {
    U64 board = 0ULL;
    for (int b=0; b<12; b++) board |= bitboards[b];
    return board;
}

U64 get_empty_squares(U64 bitboards[12]) {
    return ~get_occupied_squares(bitboards);
}

// PAWN MOVE HELPERS
U64 w_pawns_east_attacks(U64 pawns) { return shift_north_east(pawns); }
U64 w_pawns_west_attacks(U64 pawns) { return shift_north_west(pawns); }
U64 b_pawns_east_attacks(U64 pawns) { return shift_south_east(pawns); }
U64 b_pawns_west_attacks(U64 pawns) { return shift_south_west(pawns); }
U64 w_pawns_any_attacks(U64 pawns)    { return w_pawns_east_attacks(pawns) | w_pawns_west_attacks(pawns); }
U64 w_pawns_double_attacks(U64 pawns) { return w_pawns_east_attacks(pawns) & w_pawns_west_attacks(pawns); }
U64 w_pawns_single_attacks(U64 pawns) { return w_pawns_east_attacks(pawns) ^ w_pawns_west_attacks(pawns); }
U64 b_pawns_any_attacks(U64 pawns)    { return b_pawns_east_attacks(pawns) | b_pawns_west_attacks(pawns); }
U64 b_pawns_double_attacks(U64 pawns) { return b_pawns_east_attacks(pawns) & b_pawns_west_attacks(pawns); }
U64 b_pawns_single_attacks(U64 pawns) { return b_pawns_east_attacks(pawns) ^ b_pawns_west_attacks(pawns); }

U64 test_function(uint8_t square) {
    return 0ULL >> square;
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

    U64 test = test_function(e2);
    print_bitboard(test);

    return 0;
}
