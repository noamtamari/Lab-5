#include <stddef.h>

#define STDOUT 1

// Helper function to write a string
void write_string(const char *str) {
    size_t len = 0;
    while (str[len] != '\0') len++;  // Calculate length
    
    asm volatile (
        "movl $4, %%eax\n"
        "movl $1, %%ebx\n"
        "movl %0, %%ecx\n"
        "movl %1, %%edx\n"
        "int $0x80\n"
        :
        : "r"(str), "r"(len)
        : "eax", "ebx", "ecx", "edx"
    );
}

// Helper function to write a single character
void write_char(char c) {
    asm volatile (
        "movl $4, %%eax\n"
        "movl $1, %%ebx\n"
        "movl %0, %%ecx\n"
        "movl $1, %%edx\n"
        "int $0x80\n"
        :
        : "r"(&c)
        : "eax", "ebx", "ecx", "edx"
    );
}

__attribute__((noreturn))
void _start(int argc, char *argv[]) {
    write_string("=== test_args started ===\n");
    
    // Print argc
    write_string("argc = ");
    // Simple way to print argc (assumes argc < 10)
    char argc_char = '0' + argc;
    write_char(argc_char);
    write_char('\n');
    
    // Print all arguments
    for (int i = 0; i < argc; i++) {
        write_string("argv[");
        char index_char = '0' + i;
        write_char(index_char);
        write_string("] = \"");
        write_string(argv[i]);
        write_string("\"\n");
    }
    
    write_string("=== test_args finished ===\n");

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
