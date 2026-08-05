.PHONY: all
all:
	gcc -Wno-int-conversion -Wno-implicit-function-declaration -o code main.c buddy.c
