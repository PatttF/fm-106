	.text
	.file	"build/test_float.c"
	.globl	foo
	.p2align	1
	.type	foo,@function
foo:                                    ; @foo
; BB#0:
	[--sp] = rets
	call __mulsf3
	r1 = 0x3FC00000
	{rets} = [sp++]
	goto __addsf3
.Lfunc_end0:
	.size	foo, .Lfunc_end0-foo


