# ffp

A far-from-perfect chess engine in C using **bitboards** (a8=bit0 … h1=bit63).  
Single-file build, legal move generation, FEN loader, perft, a tiny alpha–beta search, and a minimal UCI loop.

Thanks to https://www.chessprogramming.org/ for being the best resource.

## Features (current)

- **Board & State**
  - 12 piece bitboards, side to move, castling rights, en-passant square, half/full-move counters
  - FEN loader (`startpos` supported)
- **Move Generation**
  - Knights/Kings via masks; Bishops/Rooks/Queens via blocker-aware rays
  - Pawns: single/double pushes (through empties), captures, promotions (Q/R/B/N), **en passant**
  - **Castling** (KQkq): empty path, no through/into check
  - **Legal** move list (king-in-check filtering)
- **Search & Eval**
  - Material-only evaluation
  - Fixed-depth alpha–beta, mate/stalemate detection
- **Perft**
  - Node counts from any position for correctness testing
- **UCI (minimal)**
  - `uci`, `isready`, `ucinewgame`, `position startpos|fen … moves …`, `go depth N`, `perft N`, `d`, `quit`

## Quick Start

### Build & run
```bash
gcc -O2 -Wall -Wextra -o ffp ffp.c && ./ffp
```
## Useful Commands
```
# Show start position + a sample search (depth 4)
./ffp

# Load a FEN and print the board
./ffp --fen "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

# Perft from current/loaded position
./ffp --perft 4

# Fixed-depth search (prints best move)
./ffp --search 5

# UCI mode for GUIs (Arena, CuteChess, etc.)
./ffp --uci
```
