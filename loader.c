#define _GNU_SOURCE
#include <elf.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/* raw int-0x80 wrapper (start.s) */
extern int system_call(int, ...);
/* stack trampoline (startup.s)   */
extern int startup(int, char **, void (*)(void));

/* ─── i386 syscall numbers ─── */
#define SYS_WRITE      4
#define SYS_OPEN       5
#define SYS_CLOSE      6
#define SYS_FSTAT    108
#define SYS_OLD_MMAP  90
#define SYS_MUNMAP    91
#define SYS_EXIT       1
#define STDERR         2

/* struct for old_mmap (single-arg variant) */
struct mmap_arg_struct {
    unsigned long addr, len, prot, flags, fd, offset;
};

/* ─── tiny helpers (no libc) ─── */
static size_t slen(const char *s){ size_t n=0; while(s&&s[n]) ++n; return n; }
static void   werr(const char *s){ system_call(SYS_WRITE, STDERR, s, slen(s)); }

/* convert 32-bit value to hex “0xXXXXXXXX” and print to stderr */
static void puthex(uint32_t v){
    char b[11] = "0x00000000";
    for (int i=9;i>=2;--i){
        int nib = v & 0xF; v >>= 4;
        b[i] = (nib<10)? ('0'+nib) : ('a'+nib-10);
    }
    werr(b);
}

/* pretty name for p_type */
static const char *ptype(uint32_t t){
    switch(t){
        case PT_LOAD:    return "LOAD    ";
        case PT_PHDR:    return "PHDR    ";
        case PT_INTERP:  return "INTERP  ";
        case PT_DYNAMIC: return "DYNAMIC ";
        case PT_NOTE:    return "NOTE    ";
        case PT_GNU_STACK:return"GNU_STK ";
        default:         return "UNKNOWN ";
    }
}

/* dump one program header (Task 1 style) */
static void dump_phdr(Elf32_Phdr *ph, void *u){
    (void)u;
    werr(ptype(ph->p_type));
    werr("Off ");  puthex(ph->p_offset);
    werr("  VA "); puthex(ph->p_vaddr);
    werr("  File ");puthex(ph->p_filesz);
    werr("  Mem "); puthex(ph->p_memsz);
    werr("  F ");
    if(ph->p_flags & PF_R) werr("R");
    if(ph->p_flags & PF_W) werr("W");
    if(ph->p_flags & PF_X) werr("E");
    werr("\n");
}

/* iterate program-header table */
static int foreach_phdr(void *base,
                        void (*cb)(Elf32_Phdr*,void*), void *u){
    Elf32_Ehdr *eh = base;
    if (!base||!cb) return -1;
    if (eh->e_ident[0]!=0x7F||eh->e_ident[1]!='E'||
        eh->e_ident[2]!='L'||eh->e_ident[3]!='F') return -1;
    if (eh->e_ident[EI_CLASS]!=ELFCLASS32) return -1;

    for(int i=0;i<eh->e_phnum;++i){
        Elf32_Phdr *ph = (void*)((char*)base+
                          eh->e_phoff+i*eh->e_phentsize);
        cb(ph,u);
    }
    return 0;
}

/* ─── segment mapper (PT_LOAD) ─── */
struct ctx{ int fd; };
static void map_one(Elf32_Phdr *ph, void *a){
    if(ph->p_type!=PT_LOAD) return;
    int fd=((struct ctx*)a)->fd;

    unsigned page = ph->p_vaddr & 0xFFFFF000;
    unsigned pad  = ph->p_vaddr & 0x00000FFF;
    unsigned len  = ph->p_memsz + pad;
    unsigned off  = ph->p_offset & 0xFFFFF000;

    int prot=0;
    if(ph->p_flags&PF_R) prot|=PROT_READ;
    if(ph->p_flags&PF_W) prot|=PROT_WRITE;
    if(ph->p_flags&PF_X) prot|=PROT_EXEC;

    struct mmap_arg_struct mm={page,len,prot,
                               MAP_PRIVATE|MAP_FIXED, fd, off};
    void *res=(void*)system_call(SYS_OLD_MMAP,&mm);
    if((unsigned long)res>=0xFFFFF000){
        werr("mmap seg fail\n");
        system_call(SYS_EXIT,1);
    }
    if(ph->p_memsz>ph->p_filesz){
        char *dst=(char*)(ph->p_vaddr+ph->p_filesz);
        unsigned n=ph->p_memsz-ph->p_filesz;
        for(unsigned i=0;i<n;++i) dst[i]=0;
    }
}

/* ─── main loader ─── */
int main(int argc,char **argv){
    if(argc<2){ werr("usage: loader <exe> [args]\n"); system_call(SYS_EXIT,1);}
    const char *path=argv[1];

    int fd=system_call(SYS_OPEN, path, O_RDONLY, 0);
    if(fd<0){ werr("open fail\n"); system_call(SYS_EXIT,1); }

    struct stat st;
    if(system_call(SYS_FSTAT,fd,&st)<0){
        werr("fstat fail\n"); system_call(SYS_EXIT,1);
    }

    struct mmap_arg_struct mmfile={0,st.st_size,PROT_READ,
                                   MAP_PRIVATE,fd,0};
    void *img=(void*)system_call(SYS_OLD_MMAP,&mmfile);
    if((unsigned long)img>=0xFFFFF000){
        werr("mmap hdr fail\n"); system_call(SYS_EXIT,1);
    }

    /* ---- Task 1 header dump ---- */
    werr("Type     Off      VA        FileSz   MemSz   Flg\n");
    foreach_phdr(img, dump_phdr, NULL);

    /* ---- map segments ---- */
    struct ctx c={fd};
    foreach_phdr(img, map_one, &c);

    /* ---- jump to entry ---- */
    Elf32_Ehdr *eh=img;
    void (*entry)(void)=(void(*)(void))eh->e_entry;
    int rc=startup(argc-1, argv+1, entry);

    /* ---- cleanup ---- */
    system_call(SYS_MUNMAP,img,st.st_size);
    system_call(SYS_CLOSE,fd);
    system_call(SYS_EXIT,rc);
    return 0;
}
