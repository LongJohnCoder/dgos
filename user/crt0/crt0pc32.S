.code64

#include <sys/cpu_bug.h>

.section .init_array, "a", @progbits
.quad __crt0_fpu32

.section .text, "ax", @progbits

__crt0_fpu32:
    fnstcw -8(%esp)
    // PC bit 9:8 == 0x0 to set 24 bit (float) precision
    andw $~0x300,-8(%esp)
    fldcw -8(%esp)
    ret