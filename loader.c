#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>    
#include <stdlib.h>   
#include <string.h>   
#include <sys/types.h> 
#include <sys/stat.h> 
#include <fcntl.h>    
#include <sys/mman.h> 
#include <unistd.h>   


#include <elf.h>

// Define a common stack size for the loaded program
#define LOADER_STACK_SIZE 0x100000 // 1MB for the loaded program's stack

// External declaration for the current process's environment variables
extern char **environ;

// Global variable to hold the file descriptor of the target ELF for mmap operations
static int g_target_elf_fd = -1;

/*
 * External declaration of the startup function implemented in startup.s.
 *
 * This function is responsible for transferring control to the loaded program.
 */
extern __attribute__((noreturn)) void startup(int argc, char **argv, unsigned int entry_point);

/*
 * get_phdr_type_string:
 *   Returns a human-readable string representing the type of a program header.
 *
 * Parameters:
 *   p_type - the program header type (Elf32_Word).
 *
 * Returns:
 *   A const char* that describes the program header type.
 */
const char* get_phdr_type_string(Elf32_Word p_type) {
    switch (p_type) {
        case PT_NULL:    return "NULL";
        case PT_LOAD:    return "LOAD";
        case PT_DYNAMIC: return "DYNAMIC";
        case PT_INTERP:  return "INTERP";
        case PT_NOTE:    return "NOTE";
        case PT_SHLIB:   return "SHLIB";
        case PT_PHDR:    return "PHDR";
        case PT_TLS:     return "TLS";
        case PT_NUM:     return "NUM";
        case PT_GNU_EH_FRAME: return "GNU_EH_FRAME";
        case PT_GNU_STACK:    return "GNU_STACK";
        case PT_GNU_RELRO:    return "GNU_RELRO";
        default:         return "UNKNOWN";
    }
}

/*
 * print_readelf_phdr_info:
 *   Callback function to print information about a program header.
 *
 * Parameters:
 *   phdr  - Pointer to the program header (Elf32_Phdr).
 *   index - Index number of the program header.
 */
void print_readelf_phdr_info(Elf32_Phdr *phdr, int index) {
    if (phdr == NULL) {
        fprintf(stderr, "Error: Passed NULL program header to print_readelf_phdr_info.\n");
        return;
    }

    printf("%-8s ", get_phdr_type_string(phdr->p_type));
    printf("0x%08x ", phdr->p_offset); // Offset
    printf("0x%08x ", phdr->p_vaddr);  // VirtAddr
    printf("0x%08x ", phdr->p_paddr);  // PhysAddr
    printf("0x%07x ", phdr->p_filesz); // FileSiz
    printf("0x%07x ", phdr->p_memsz);  // MemSiz

    printf("%c%c%c ",
           (phdr->p_flags & PF_R) ? 'R' : ' ',
           (phdr->p_flags & PF_W) ? 'W' : ' ',
           (phdr->p_flags & PF_X) ? 'E' : ' ');

    printf("0x%x ", phdr->p_align);

    if (phdr->p_type == PT_LOAD) {
        int prot_flags = 0;
        if (phdr->p_flags & PF_R) prot_flags |= PROT_READ;
        if (phdr->p_flags & PF_W) prot_flags |= PROT_WRITE;
        if (phdr->p_flags & PF_X) prot_flags |= PROT_EXEC;

        printf("%s%s%s",
               (prot_flags & PROT_READ)  ? "PROT_READ"  : "",
               (prot_flags & PROT_WRITE) ? "|PROT_WRITE" : "",
               (prot_flags & PROT_EXEC)  ? "|PROT_EXEC" : "");

        if (prot_flags == 0) {
            printf("0");
        }
        
        printf(" MAP_PRIVATE|MAP_FIXED");
    } else {
        printf("-                        ");
    }
    printf("\n");
}

/*
 * foreach_phdr:
 *   Iterates over all the program headers in the ELF file and applies a provided
 *   function to each header.
 *
 * Parameters:
 *   map_start - Base address where the ELF file is mapped.
 *   func      - Callback function to apply to each program header.
 *   arg       - Additional argument for the callback 
 *
 * Returns:
 *   0 on success, or -1 on error.
 */
int foreach_phdr(void *map_start, void (*func)(Elf32_Phdr *, int), int arg) {
    Elf32_Ehdr *elf_header = (Elf32_Ehdr *)map_start;
// Check if the ELF header is valid
    if (memcmp(elf_header->e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "Error: Not an ELF file (bad magic number).\n");
        return -1;
    }
    if (elf_header->e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "Error: Not a 32-bit ELF file. This loader only supports 32-bit.\n");
        return -1;
    }
    if (elf_header->e_type != ET_EXEC && elf_header->e_type != ET_DYN) {
        fprintf(stderr, "Error: Not an executable ELF file (e_type = %x).\n", elf_header->e_type);
        return -1;
    }

    Elf32_Off phoff = elf_header->e_phoff; // Program header offset
    Elf32_Half phnum = elf_header->e_phnum; // Number of program headers
    Elf32_Half phentsize = elf_header->e_phentsize; // Size of each program header

    if (phnum == 0) {
        fprintf(stderr, "Warning: No program headers found in the ELF file.\n");
        return 0;
    }

    Elf32_Phdr *current_phdr = (Elf32_Phdr *)((char *)map_start + phoff); // Start of program headers 
    // Iterate through each program header and apply the callback function
    for (int i = 0; i < phnum; ++i) {
        func(current_phdr, i);
        current_phdr = (Elf32_Phdr *)((char *)current_phdr + phentsize);
    }
    return 0;
}

/*
 * load_phdr:
 *   Loads a single PT_LOAD segment described by the provided program header.
 *
 * Parameters:
 *   phdr - Pointer to the program header (Elf32_Phdr).
 *   fd   - File descriptor of the ELF file.
 *
 * Operation:
 *   Maps the segment into memory using mmap() at the proper virtual address with
 *   the required protections. If the segment's memory size is larger than its file
 *   size, the remainder (typically BSS) is zeroed.
 */
void load_phdr(Elf32_Phdr *phdr, int fd) {
    if (phdr == NULL) {
        fprintf(stderr, "Error: load_phdr received NULL program header.\n");
        return;
    }

    if (phdr->p_type == PT_LOAD) {
        unsigned int vaddr = phdr->p_vaddr;
        size_t mem_size = phdr->p_memsz;
        off_t file_offset = phdr->p_offset;
        size_t file_size_in_segment = phdr->p_filesz;

        unsigned long page_size = sysconf(_SC_PAGESIZE);
        unsigned long page_start_addr = vaddr & ~(page_size - 1);
        unsigned long offset_in_page = vaddr & (page_size - 1);

        unsigned long page_file_offset = file_offset & ~(page_size - 1);

        size_t map_len = file_size_in_segment + offset_in_page;
        map_len = (map_len + page_size - 1) & ~(page_size - 1);

        int prot_flags = 0;
        if (phdr->p_flags & PF_R) prot_flags |= PROT_READ;
        if (phdr->p_flags & PF_W) prot_flags |= PROT_WRITE;
        if (phdr->p_flags & PF_X) prot_flags |= PROT_EXEC;

        int map_flags = MAP_PRIVATE | MAP_FIXED;

        void *mapped_region = mmap((void *)page_start_addr, map_len, prot_flags, map_flags, fd, page_file_offset);
        printf("Mapped segment at 0x%lx (len 0x%lx) for entry check\n",
               page_start_addr, (unsigned long)map_len);

        if (mapped_region == MAP_FAILED) {
            perror("Error mapping segment in load_phdr");
            fprintf(stderr, "Failed to map segment VAddr: 0x%x, FileSiz: 0x%x, MemSiz: 0x%x, Offset: 0x%x\n",
                    vaddr, (unsigned int)file_size_in_segment, (unsigned int)mem_size, (unsigned int)file_offset);
            exit(EXIT_FAILURE);
        }
        
        printf("Mapped PT_LOAD segment: VAddr: 0x%x (Page Aligned: 0x%lx), Map Len: 0x%lx\n",
               vaddr, page_start_addr, (unsigned long)map_len);

        unsigned long zero_start_addr = vaddr + file_size_in_segment;
        unsigned long zero_end_addr = vaddr + mem_size;

        if (zero_end_addr > zero_start_addr) {
            size_t zero_size = zero_end_addr - zero_start_addr;
            printf("Zeroing BSS from 0x%lx for 0x%lx bytes\n",
                   zero_start_addr, (unsigned long)zero_size);
            memset((void *)zero_start_addr, 0, zero_size);
        }
    }
}

/*
 * loader_map_callback:
 *   Wrapper function for foreach_phdr. It calls load_phdr for each PT_LOAD header.
 *
 * Parameters:
 *   phdr  - Pointer to the program header.
 *   index - Header index (not used).
 */
void loader_map_callback(Elf32_Phdr *phdr, int index) {
    load_phdr(phdr, g_target_elf_fd);
}

/*
 * execute_loaded_elf:
 *   Prepares the command-line arguments for the loaded program and transfers
 *   control to its entry point using the startup routine.
 *
 * Parameters:
 *   elf_header   - Pointer to the ELF header of the loaded program.
 *   loader_argc  - Argument count passed to the loader.
 *   loader_argv  - Argument vector passed to the loader.
 *
 * Operation:
 *   The loaded program sees its argv starting from loader_argv[1].
 *   For example, if the loader is executed as:
 *       ./loader ./test_args hello world 123
 *   The loaded program will run with:
 *       argc = 4; argv[0] = "./test_args", argv[1] = "hello", argv[2] = "world", argv[3] = "123"
 *
 *   This function prints diagnostic information and then calls startup().
 */
__attribute__((noreturn))
void execute_loaded_elf(Elf32_Ehdr *elf_header, int loader_argc, char *loader_argv[]) {
    unsigned int entry = elf_header->e_entry;
    
    // Prepare argc and argv for the loaded program:
    int target_argc = loader_argc - 1;  // Skip "./loader"
    char **target_argv = &loader_argv[1];  // Point to "test_args" and its args
    
    printf("=== DEBUG: Argument Analysis ===\n");
    printf("Original loader_argc: %d\n", loader_argc);
    printf("Original loader arguments:\n");
    for (int i = 0; i < loader_argc; i++) {
        printf("  loader_argv[%d] = \"%s\"\n", i, loader_argv[i]);
    }
    printf("\nPrepared for loaded program:\n");
    printf("target_argc: %d\n", target_argc);
    printf("target_argv points to: &loader_argv[1]\n");
    for (int i = 0; i < target_argc; i++) {
        printf("  target_argv[%d] = \"%s\"\n", i, target_argv[i]);
    }
    printf("=== END DEBUG ===\n\n");
    
    printf("Transferring control to 0x%x with argc=%d\n", entry, target_argc);
    printf("Entry point address: 0x%x\n", elf_header->e_entry);
    
    // Print arguments being passed
    printf("Arguments being passed to loaded program:\n");
    for (int i = 0; i < target_argc; i++) {
        printf("  argv[%d] = \"%s\"\n", i, target_argv[i]);
    }
    
    // Call startup with signature: startup(argc, argv, entry)
    startup(target_argc, target_argv, entry);
    __builtin_unreachable();
}

/*
 * main:
 *   Entry point for the loader.
 *
 * Operation:
 *   - Opens the specified ELF file.
 *   - Maps the file into memory.
 *   - Verifies the ELF header and prints all program header information.
 *   - Iterates over and maps all PT_LOAD segments.
 *   - Transfers control to the loaded executable.
 *
 * Parameters (via command-line):
 *   argv[0] - loader executable
 *   argv[1] - target 32-bit ELF file to load
 *   argv[2...] - Arguments for the target program.
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <32bit_ELF_executable_file> [args_for_loaded_program...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *file_path = argv[1];
    int fd = -1;
    struct stat st;  
    void *map_start_for_headers = NULL;

    fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        perror("Error opening ELF file");
        return EXIT_FAILURE;
    }
    g_target_elf_fd = fd;

    if (fstat(fd, &st) == -1) {  
        perror("Error getting ELF file size");
        return EXIT_FAILURE;
    }

    map_start_for_headers = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_start_for_headers == MAP_FAILED) {
        perror("Error mapping ELF file for header parsing");
        return EXIT_FAILURE;
    }

    Elf32_Ehdr *elf_header = (Elf32_Ehdr *)map_start_for_headers;

    printf("--- Task 1a/1b: Program Header Information (All Headers) ---\n");
    printf("Type     Offset   VirtAddr   PhysAddr   FileSiz  MemSiz   Flg Align   Prot Flags       Map Flags\n");
    foreach_phdr(map_start_for_headers, print_readelf_phdr_info, 0);
    printf("----------------------------------------------------------\n\n");

    Elf32_Phdr *current_phdr_check = (Elf32_Phdr *)((char *)map_start_for_headers + elf_header->e_phoff);
    for (int i = 0; i < elf_header->e_phnum; ++i) {
        if (current_phdr_check->p_type == PT_DYNAMIC || current_phdr_check->p_type == PT_INTERP) {
            fprintf(stderr, "Error: This loader only supports static linked executables. Found %s program header.\n",
                    get_phdr_type_string(current_phdr_check->p_type));
            return EXIT_FAILURE;
        }
        current_phdr_check = (Elf32_Phdr *)((char *)current_phdr_check + elf_header->e_phentsize);
    }

    printf("--- Task 2b: Mapping PT_LOAD segments ---\n");
    foreach_phdr(map_start_for_headers, loader_map_callback, 0);
    printf("----------------------------------------\n\n");

    printf("--- Transferring Control ---\n");
    execute_loaded_elf(elf_header, argc, argv);
    printf("----------------------------\n");

    if (map_start_for_headers != MAP_FAILED) {
        if (munmap(map_start_for_headers, st.st_size) == -1) {
            perror("Error unmapping temporary header map");
        }
    }
    if (fd != -1) {
        if (close(fd) == -1) {
            perror("Error closing ELF file descriptor");
        }
    }

    return EXIT_SUCCESS;
}