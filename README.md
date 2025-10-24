# ffp

A far-from-perfect chess engine in C using **bitboards** (a8=bit0 … h1=bit63).  
Single-file build, legal move generation, FEN loader, perft, a tiny alpha–beta search, and a minimal UCI loop.

Thanks to https://www.chessprogramming.org/ for being the best resource!

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

## Building

ffp has no external dependencies beyond a C11-capable compiler and the C
standard library. The engine builds cleanly with GCC and Clang on Linux and
macOS.

# Optimised build (default make target)
make

# Debug build with assertions and symbols
make debug

# Or compile manually
gcc -O2 -Wall -Wextra -o ffp ffp.c
```

The resulting `./ffp` binary is self-contained and ready to execute from the
project root.

## Command-line usage

Running the binary without arguments prints the start position and searches it
to depth 4:

```bash
./ffp
```

For scripted usage, the CLI accepts the following switches:

| Option | Description |
| ------ | ----------- |
| `--help` | Print a short summary of the CLI commands. |
| `--fen "<FEN>"` | Load a custom position before executing another command. |
| `--perft N` | Count legal nodes to depth `N` from the current position. Prints timing and kilo-nodes/sec. |
| `--search N` | Run a fixed-depth alpha–beta search and report the best move found at depth `N`. |
| `--uci` | Start the minimal UCI loop for use with chess GUIs. |

Arguments are processed in order, so you can combine them to stage a position
and then analyse it. For example, to search a custom FEN at depth 6:

```bash
./ffp --fen "rnbqkb1r/pppp1ppp/4pn2/8/2PP4/5NP1/PP2PPBP/RNBQK2R b KQkq - 4 4" --search 6
```

Similarly, to sanity-check move generation with perft from a FEN:

```bash
./ffp --fen "r4rk1/1pp1qppp/p1np1n2/2b1p3/2B1P3/2NP1N2/PPPQ1PPP/2KR3R w - - 0 1" --perft 4
```

Perft output includes the node count, elapsed time, and throughput so you can
compare performance across changes or platforms.

## Using the UCI mode

Most chess GUIs can drive ffp through the Universal Chess Interface. Launch the
engine in UCI mode manually to interact over standard input/output:

```bash
./ffp --uci
```

Within a GUI (Arena, CuteChess, Banksia, etc.), configure an engine pointing to
the compiled `ffp` binary and enable "Start with UCI" or equivalent. The engine
implements the subset of the protocol required for casual analysis: `uci`,
`isready`, `ucinewgame`, `position`, `go depth N`, `perft`, `d`, and `quit`.

## Troubleshooting

- **Compilation warnings** Build with `make debug` during development to pick
  up extra diagnostics (`-Wall -Wextra`).
- **Quoting FEN strings** Remember to wrap FENs that include spaces in double
  quotes when calling from a shell.
- **Performance checks** Use `--perft` at increasing depths to verify that
  recent changes did not introduce illegal move generation.