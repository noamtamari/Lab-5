#include <stddef.h>

#define STDOUT 1

__attribute__((noreturn))
void _start(int argc, char *argv[]) {
    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        size_t len = 0;
        while (arg[len] != '\0') len++;

        asm volatile (
            "movl $4, %%eax\n"
            "movl $1, %%ebx\n"
            "movl %0, %%ecx\n"
            "movl %1, %%edx\n"
            "int $0x80\n"
            :
            : "r"(arg), "r"(len)
            : "eax", "ebx", "ecx", "edx"
        );

        const char nl = '\n';
        asm volatile (
            "movl $4, %%eax\n"
            "movl $1, %%ebx\n"
            "movl %0, %%ecx\n"
            "movl $1, %%edx\n"
            "int $0x80\n"
            :
            : "r"(&nl)
            : "eax", "ebx", "ecx", "edx"
        );
    }

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
