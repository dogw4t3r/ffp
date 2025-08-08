# ffp

A far-from-perfect attempt to create a chess engine in C.

## Entries
### Aug, 7 - Board representation
Engines make many calculations during a game and must therefore be efficient. Decide on how a chessboard and its information should be stored.
Arrays quickly come to mind, but *Bitboards* are the better solution. Bitboards are 64-bit long integers that 
can store two states for each of the 64 squares in a chess board. 1 represents a piece and 0 an empty square.

```c
typedef uint64_t Bitboard;
Bitboard board = 0ULL; // unsigned long long literal → already matches uint64_t
```

> **0** translates to board position
```
8 0  0  0  0  0  0  0  0
7 0  0  0  0  0  0  0  0
6 0  0  0  0  0  0  0  0
5 0  0  0  0  0  0  0  0
4 0  0  0  0  0  0  0  0
3 0  0  0  0  0  0  0  0
2 0  0  0  0  0  0  0  0
1 0  0  0  0  0  0  0  0
  A  B  C  D  E  F  G  H
```

Now we can determine which squares are occupied or empty. To represent a full chess board, 12 individual bitboards are required. One bitboard for each type of piece per color (12 in total, 6 for white, 6 for black).
```c
// Instantiation of bitboards
// Starting position for each piece type as hexadecimal

Bitboard bitboards[12];

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
We can now put together a board. 

> Bitboard representation of white pawns and black bishops
```
8 0  0  0  0  0  0  0  0         8 0  1  0  0  0  0  1  0
7 0  0  0  0  0  0  0  0         7 0  0  0  0  0  0  0  0
6 0  0  0  0  0  0  0  0         6 0  0  0  0  0  0  0  0
5 0  0  0  0  0  0  0  0         5 0  0  0  0  0  0  0  0
4 0  0  0  0  0  0  0  0         4 0  0  0  0  0  0  0  0
3 0  0  0  0  0  0  0  0         3 0  0  0  0  0  0  0  0
2 1  1  1  1  1  1  1  1         2 0  0  0  0  0  0  0  0
1 0  0  0  0  0  0  0  0         1 0  0  0  0  0  0  0  0
  A  B  C  D  E  F  G  H           A  B  C  D  E  F  G  H
```
> Assign one character to each bitboard and combine them to visualize a complete board
```
8 r  n  b  q  k  b  n  r
7 p  p  p  p  p  p  p  p
6 .  .  .  .  .  .  .  .
5 .  .  .  .  .  .  .  .
4 .  .  .  .  .  .  .  .
3 .  .  .  .  .  .  .  .
2 P  P  P  P  P  P  P  P
1 R  N  B  Q  K  B  N  R
  A  B  C  D  E  F  G  H
```
