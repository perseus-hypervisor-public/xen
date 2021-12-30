#include <linux/linkage.h>
#include <asm/assembler.h>

#define DSM_IMM 0xEAE

ENTRY(hypercall)
	mov x16, x0
	mov x0, x1
	mov x1, x2
	mov x2, x3
	mov x3, x4
	mov x4, x5
	hvc DSM_IMM
	ret
ENDPROC(hypercall);
