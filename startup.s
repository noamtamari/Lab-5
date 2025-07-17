section .text
global startup:function startup_end - startup

startup:
    push    ebp
    mov     ebp, esp
    
    ; Arguments: startup(argc, argv, entry_point)
    ; [ebp+8]  = argc
    ; [ebp+12] = argv  
    ; [ebp+16] = entry_point
    
    mov     eax, [ebp+8]    ; argc
    mov     ebx, [ebp+12]   ; argv
    mov     ecx, [ebp+16]   ; entry point
    
    ; Set up stack for the loaded program's _start function
    ; Standard calling convention: arguments pushed right to left
    ; _start(int argc, char *argv[]) means push argv first, then argc
    
    ; Reset stack pointer to set up clean environment
    mov     esp, ebp
    pop     ebp
    
    ; Push arguments in reverse order for calling convention
    push    ebx             ; push argv (second parameter)
    push    eax             ; push argc (first parameter)
    
    ; Jump to the loaded program's entry point
    jmp     ecx

startup_end: