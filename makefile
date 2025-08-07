all:
	@gcc -O2 bitboard.c -o bitboard

debug:
	@gcc bitboard.c -o bitboard
