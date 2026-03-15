.file	"switch_stack_linux.s"
.text
.globl	corevmoc_switch_stack
.type	corevmoc_switch_stack, @function
corevmoc_switch_stack:
   movq %rsp, -16(%rdi)
   leaq -16(%rdi), %rsp
   movq %rdx, %rdi
   callq *%rsi
   mov (%rsp), %rsp
   retq
.size	corevmoc_switch_stack, .-corevmoc_switch_stack
.section	.note.GNU-stack,"",@progbits
