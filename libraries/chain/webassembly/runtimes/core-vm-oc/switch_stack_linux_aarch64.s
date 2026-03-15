.file	"switch_stack_linux_aarch64.s"
.text
.globl	corevmoc_switch_stack
.type	corevmoc_switch_stack, @function
// void corevmoc_switch_stack(void* new_stack_top, void(*func)(void*), void* arg)
// x0 = new_stack_top, x1 = function pointer, x2 = argument
corevmoc_switch_stack:
   // Save frame pointer and link register (AAPCS64 requires 16-byte stack alignment)
   stp  x29, x30, [sp, #-16]!
   mov  x29, sp

   // Save current SP at top of new stack (for later restoration)
   mov  x9, sp
   str  x9, [x0, #-16]

   // Switch to new stack
   sub  sp, x0, #16

   // Set up argument: x2 (third param) -> x0 (first param for called function)
   mov  x0, x2

   // Call function pointer
   blr  x1

   // Restore old SP from top of stack
   ldr  x9, [sp]
   mov  sp, x9

   // Restore frame pointer and link register, return
   ldp  x29, x30, [sp], #16
   ret
.size	corevmoc_switch_stack, .-corevmoc_switch_stack
.section	.note.GNU-stack,"",@progbits
