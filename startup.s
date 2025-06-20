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
    
    ; Push arguments for the loaded program's _start function
    ; The loaded program expects: _start(int argc, char *argv[])
    push    ebx             ; push argv
    push    eax             ; push argc
    
    ; Jump to the loaded program's entry point
    jmp     ecx

startup_end: