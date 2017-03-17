.section .entry, "ax"

.global entry
.hidden entry
entry:
	xor %ebp,%ebp

	# Enable SSE (CR4_OFXSR_BIT) and SSE exceptions CR4_OSXMMEX)
	# This must be done before jumping into C code
	mov %cr4,%rax
	or $0x600,%rax
	mov %rax,%cr4

	# Initialize FPU to 64 bit precision,
	# all exceptions masked, round to nearest
	fninit
	push $((3 << 10) | 0x3F)
	fldcw (%rsp)
	add $8,%rax

	push %rdx
	push %rcx

	# See if branch trace is available
#	mov $0x1A0,%ecx
#	rdmsr
#	test $0x800,%eax
#	jz 0f
#
#	# Enable last branch records
#	mov $0x1D9,%ecx
#	rdmsr
#	or $1,%eax
#	wrmsr
#0:

	# See if this is the bootstrap processor
	mov $0x1B,%ecx
	rdmsr
	pop %rcx
	pop %rdx
	test $0x100,%eax
	jnz 0f

	# Align stack
	xor %ebp,%ebp
	push $0

	# MP processor entry
	jmp mp_main

0:

	lea kernel_stack(%rip),%rax
	add kernel_stack_size(%rip),%rax
	mov %rax,%rsp

	# Store the physical memory map address
	# passed in from bootloader
	mov %ecx,%edx
	shr $20,%edx
	mov %rdx,phys_mem_map_count(%rip)
	and $0x000FFFFF,%rcx
	mov %rcx,phys_mem_map(%rip)

	call e9debug_init

	xor %edi,%edi
	call cpu_init

	# Call the constructors
	lea ___init_st(%rip),%rdi
	lea ___init_en(%rip),%rsi
	call invoke_function_array

	xor %edi,%edi
	call cpu_hw_init

	# Initialize early-initialized devices
	mov $'E',%edi
	call callout_call

	call main

	mov %rax,%rdi
	call exit

.global exit
.hidden exit
exit:
	# Ignore exitcode
	# Kernel exit just calls destructors
	# and deliberately hangs
0:
	lea ___fini_st(%rip),%rdi
	lea ___fini_en(%rip),%rsi
	call invoke_function_array

	call halt_forever

invoke_function_array:
	push %rbx
	push %rbp
	mov %rdi,%rbx
	mov %rsi,%rbp
0:
	cmp %rbx,%rbp
	jbe 0f
	call *(%rbx)
	add $8,%rbx
	jmp 0b
0:
	pop %rbp
	pop %rbx
	ret

# Callout to initialize AP CPU
.global mp_main
.hidden mp_main
mp_main:
	mov $'S',%edi
	call callout_call
	ret
