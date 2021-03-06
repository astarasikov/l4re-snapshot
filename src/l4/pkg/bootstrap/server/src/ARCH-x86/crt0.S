/*
 * (c) 2009 Adam Lackorzynski <adam@os.inf.tu-dresden.de>,
 *          Frank Mehnert <fm3@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */
#include <l4/sys/compiler.h>
	
	.section .init

	.globl	_start
_start:
#ifdef REALMODE_LOADING
	cld
	cli
	mov	$(3 * 8), %eax
	mov	%eax, %ds
	mov	%eax, %es
	mov	%eax, %fs
	mov	%eax, %gs
	mov	%eax, %ss
	lss	_stack_seg, %esp
#else
	leal	_stack,%esp
#endif
	pushl   %esi	/* Pointer to real mode or Xen start_info pointer */
	pushl	%eax
	pushl	%ebx

#ifndef XEN
	/* initialize vector for exception 6 */
	movl	$trap6_entry,%eax
	movw	%ax, _idt_offset_low
	shrl	$16, %eax
	movw	%ax, _idt_offset_high
	movw	%cs, %ax
	movw	%ax, _idt_selector

	/* load tiny interrupt descriptor table to catch exception 6 */
	lidtl	_idtdesc
#endif

	pushl	$_exit
	jmp	__main

	/* Show an error message and wait for keypress to reboot. */
trap6_entry:
	pusha
	cld

	/* print out warning */
	movl	$trap6_warning, %esi
	movl	$(0xb8000 + (24*80*2)), %edi
	movb	$0x0F, %ah
	
1:	/* read next character from string */
	lodsb

	/* check for end-of-string */
	cmpb	$0, %al
	je	3f
	
	/* check for newline */
	cmpb	$'\n', %al
	jne	2f
	
	pushl	%eax
	pushl	%esi
	
	/* move screen upwards 1 line */
	movl	$((24*80*2)/4), %ecx
	movl	$(0xb8000 + 1*80*2), %esi
	movl	$0xb8000, %edi
	rep	movsl

	/* clear last line of screen */
	movl	$((1*80*2)/4), %ecx
	movl	$0x07200720, %eax
	rep	stosl

	popl	%esi
	popl	%eax
	
	/* jump to new line */
	movl	$(0xb8000 + (24*80*2)), %edi
	jmp	1b
	
2:	/* print character */
	stosw
	jmp	1b

3:	/* wait for keypress */
	inb	$0x64, %al
	testb	$0x01, %al
	je	3b

	movb	%al, %ah

	/* empty keyboard buffer */
	inb	$0x60, %al
	
	/* ignore PS/2 mouse events */
	testb	$0x20, %ah
	jne	3b

	jmp	reboot


	/* the warning text */
trap6_warning:
	.ascii	"\n"
	.ascii	"Bootstrap: Invalid opcode detected! Be sure "
	.ascii	"that you don't use any CPU\n"
	.ascii	"           optimization flags like -march=i686 "
	.ascii	"when compiling Bootstrap and\n"
	.ascii	"           the depending libraries.\n"
	.ascii	"\n"
	.asciz	"           Press any key to reboot..."

        /* MultiBoot header - see multiboot.h.  */
	.p2align(2)

#define MULTIBOOT_MEMORY_INFO 0x00000002
_mb_header:
        .long   0x1BADB002		/* magic */
        .long   MULTIBOOT_MEMORY_INFO	/* flags: AOUT_KLUDGE */
        .long   0 - 0x1BADB002 - MULTIBOOT_MEMORY_INFO

#ifndef XEN
	.word	0
_idtdesc:
	.word	(7*8)-1
	.long	_idt

_idt:
	.word	0,0,0,0			/* trap 0 */
	.word	0,0,0,0			/* trap 1 */
	.word	0,0,0,0			/* trap 2 */
	.word	0,0,0,0			/* trap 3 */
	.word	0,0,0,0			/* trap 4 */
	.word	0,0,0,0			/* trap 5 */

_idt_offset_low:
	.word	0
_idt_selector:
	.word	0
	.byte	0
	.byte	0xee
_idt_offset_high:
	.word	0
#endif

#ifdef REALMODE_LOADING
_stack_seg:
	.long	_stack
	.long	(3 * 8)
#endif

	.bss
	.space	8192
	.globl	_stack
_stack:
