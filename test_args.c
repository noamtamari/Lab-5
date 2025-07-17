#include <stddef.h>

#define STDOUT 1

__attribute__((noreturn))
void _start(int argc, char *argv[]) {
    const char *message = "Roei and Noam Load the ELF file give them 100\n";
    size_t len = 47; 
    asm volatile (
        "movl $4, %%eax\n"
        "movl $1, %%ebx\n"
        "movl %0, %%ecx\n"
        "movl %1, %%edx\n"
        "int $0x80\n"
        :
        : "r"(message), "r"(len)
        : "eax", "ebx", "ecx", "edx"
    );

    asm volatile (
        "movl $1, %%eax\n"
        "xorl %%ebx, %%ebx\n"
        "int $0x80\n"
        :
        :
        : "eax", "ebx"
    );

    __builtin_unreachable();
    while (1) {}
}
