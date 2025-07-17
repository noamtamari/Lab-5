# Makefile for Lab 5 - Loader - Debugging

# Default target: builds the executable
all: loader

# Rule to link the final executable
loader: loader.o start.o startup.o
	gcc -m32 -nostartfiles -T linking_script -lc loader.o start.o startup.o -o loader

# Rule to compile loader.c
loader.o: loader.c
	gcc -m32 -Wall -g -c -fno-stack-protector loader.c -o loader.o

# Rule to assemble start.s using NASM
start.o: start.s
	nasm -f elf32 start.s -o start.o

# Rule to assemble startup.s using NASM
startup.o: startup.s
	nasm -f elf32 startup.s -o startup.o

# Clean target: removes compiled files
clean:
	rm -f loader loader.o start.o startup.o
	rm -f *.o

.PHONY: all clean
