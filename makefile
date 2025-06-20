# Makefile for Lab 5 - Loader - Debugging

# Compiler to use
CC = gcc
NASM = nasm # NASM assembler

# Compilation flags for C source
# -m32: Compile for 32-bit architecture
# -Wall: Enable all common warnings
# -g: Include debugging information
# -c: Compile only, do not link
# -fno-stack-protector: Often used in bare-metal or custom startups
CFLAGS = -m32 -Wall -g -c -fno-stack-protector

# Assembler flags for NASM (no -m32 needed as -f elf32 handles it)
AFLAGS_NASM = -f elf32

# Linker flags
# -nostartfiles: Do not link with the standard system startup files (like crt0.o)
#                We provide our own _start in start.s
# -T linking_script: Use the custom linker script
# -lc: Link with the standard C library (for printf, mmap, etc., used by the loader itself)
LDFLAGS = -m32 -nostartfiles -T linking_script -lc

# Source files
C_SRCS = loader.c
ASM_SRCS = start.s startup.s # Both start.s (for loader) and startup.s (for target)

# Object files (automatically derived from source files)
OBJS = $(C_SRCS:.c=.o) $(ASM_SRCS:.s=.o)

# Executable name
TARGET = loader

# Default target: builds the executable
all: $(TARGET)

# Rule to link the final executable
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $(TARGET)

# Rule to compile C source files
%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

# Rule to assemble assembly source files using NASM
%.o: %.s
	$(NASM) $(AFLAGS_NASM) $< -o $@

# Clean target: removes compiled files
clean:
	rm -f $(TARGET) $(OBJS)
	rm -f *.o

.PHONY: all clean
