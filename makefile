all:
	@gcc -O2 dog.c -o dog

debug:
	@gcc dog.c -o dog
