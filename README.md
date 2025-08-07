# DOG - Chess Engine

A project to document the development of a Chess engine using C.

## Entries
### Aug, 7
Professional chess engines often evaluate billions of positions during a single game. To maximize efficiency, developers have capitalized on the fact that a chessboard has exactly 64 squares — which can be naturally represented using 64-bit integers (such as ```unsigned long long``` in C), with one bit corresponding to each square. In chess programming, these integers are known as *bitboards*.

Since bits are either only 1 or 0, they are perfect for telling if a square is occupied or not. An example:

```unsigned long long board = 0xffff00000000ffffULL```

translates to board position
```
8 1  1  1  1  1  1  1  1
7 1  1  1  1  1  1  1  1
6 0  0  0  0  0  0  0  0
5 0  0  0  0  0  0  0  0
4 0  0  0  0  0  0  0  0
3 0  0  0  0  0  0  0  0
2 1  1  1  1  1  1  1  1
1 1  1  1  1  1  1  1  1
  A  B  C  D  E  F  G  H
```

While a single bitboard can represent which squares are occupied or unoccupied, it does not provide enough information to determine the position of every piece throughout a game. To differentiate between all the various pieces of both players, the engine must track them separately. The most straightforward way to achieve this is by using individual bitboards for each piece type and color, and performing bitwise operations to combine or analyze them as needed.

One bitboard for every piece type of each color with respective starting positions denoted as hexadecimal (HEX) values:
```
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
```
