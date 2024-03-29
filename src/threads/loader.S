/* This file is derived from source code used in MIT's 6.828
   course.  The original copyright notice is reproduced in full
   below. */

/*
 * Copyright (C) 1997 Massachusetts Institute of Technology
 *
 * This software is being provided by the copyright holders under the
 * following license. By obtaining, using and/or copying this software,
 * you agree that you have read, understood, and will comply with the
 * following terms and conditions:
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose and without fee or royalty is
 * hereby granted, provided that the full text of this NOTICE appears on
 * ALL copies of the software and documentation or portions thereof,
 * including modifications, that you make.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS," AND COPYRIGHT HOLDERS MAKE NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED. BY WAY OF EXAMPLE,
 * BUT NOT LIMITATION, COPYRIGHT HOLDERS MAKE NO REPRESENTATIONS OR
 * WARRANTIES OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE OR
 * THAT THE USE OF THE SOFTWARE OR DOCUMENTATION WILL NOT INFRINGE ANY
 * THIRD PARTY PATENTS, COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS. COPYRIGHT
 * HOLDERS WILL BEAR NO LIABILITY FOR ANY USE OF THIS SOFTWARE OR
 * DOCUMENTATION.
 *
 * The name and trademarks of copyright holders may NOT be used in
 * advertising or publicity pertaining to the software without specific,
 * written prior permission. Title to copyright in this software and any
 * associated documentation will at all times remain with copyright
 * holders. See the file AUTHORS which should have accompanied this software
 * for a list of all copyright holders.
 *
 * This file may be derived from previously copyrighted software. This
 * copyright applies only to those changes made by the copyright
 * holders listed in the AUTHORS file. The rest of this file is covered by
 * the copyright notices, if any, listed below.
 */

#include "threads/loader.h"
	
#### Kernel loader.

#### This code should be stored in the first sector of the hard disk.
#### When the BIOS runs, it loads this code at physical address
#### 0x7c00-0x7e00 (512 bytes).  Then it jumps to the beginning of it,
#### in real mode.  This code switches into protected mode (32-bit
#### mode) so that all of memory can accessed, loads the kernel into
#### memory, and jumps to the first byte of the kernel, where start.S
#### is linked.
	
/* Flags in control register 0. */
#define CR0_PE 0x00000001      /* Protection Enable. */
#define CR0_EM 0x00000004      /* (Floating-point) Emulation. */
#define CR0_PG 0x80000000      /* Paging. */
#define CR0_WP 0x00010000      /* Write-Protect enable in kernel mode. */


.globl start
start:
	
# Code runs in real mode, which is a 16-bit segment.
	.code16

# Disable interrupts, because we will not be prepared to handle them
# in protected mode until much later.
# String instructions go upward (e.g. for "rep stosl" below).

	cli
	cld

# Set up data segments.

	subw %ax, %ax
	movw %ax, %es
	movw %ax, %ds

# Set up stack segment.
# Stack grows downward starting from us.
# We don't ever use the stack, but we call into the BIOS,
# which might.

	movw %ax, %ss
	movw $0x7c00, %sp
	
#### Enable A20.  Address line 20 is tied to low when the machine
#### boots, which prevents addressing memory about 1 MB.  This code
#### fixes it.
	
# Poll status register while busy.

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

# Send command for writing output port.

	movb $0xd1, %al
	outb %al, $0x64

# Poll status register while busy.

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

# Enable A20 line.

	movb $0xdf, %al
	outb %al, $0x60

#### Get memory size, via interrupt 15h function 88h.  Returns CF
#### clear if successful, with AX = (kB of physical memory) - 1024.
#### This only works for memory sizes <= 65 MB, which should be fine
#### for our purposes.  We cap memory at 64 MB because that's all we
#### prepare page tables for, below.
	
	movb $0x88, %ah
	int $0x15
	jc panic
	cli			# BIOS might have enabled interrupts
	addl $1024, %eax	# Total kB memory
	cmp $0x10000, %eax	# Cap at 64 MB
	jbe 1f
	mov $0x10000, %eax
1:	shrl $2, %eax		# Total 4 kB pages
	movl %eax, ram_pgs
	
#### Create temporary page directory and page table and set page
#### directory base register.

# Create page directory at 64 kB and fill with zeroes.
	mov $0x1000, %ax
	mov %ax, %es
	subl %eax, %eax
	subl %edi, %edi
	movl $0x400, %ecx
	rep stosl

# Add PDEs to point to PTEs for the first 64 MB of RAM.
# Also add identical PDEs starting at LOADER_PHYS_BASE.
# See [IA32-v3a] section 3.7.6 "Page-Directory and Page-Table Entries"
# for a description of the bits in %eax.
	

	movl $0x11007, %eax
	movl $0x11, %ecx
	subl %edi, %edi
1:	movl %eax, %es:(%di)
	movl %eax, %es:LOADER_PHYS_BASE >> 20(%di)
	addw $4, %di
	addl $0x1000, %eax
	loop 1b

# Set up one-to-map linear to physical map for the first 64 MB of RAM.
# See [IA32-v3a] section 3.7.6 "Page-Directory and Page-Table Entries"
# for a description of the bits in %eax.

	movw $0x1100, %ax
	movw %ax, %es
	movl $0x7, %eax
	movl $0x4000, %ecx
	subl %edi, %edi
1:	movl %eax, %es:(%di)
	addw $4, %di
	addl $0x1000, %eax
	loop 1b

# Set page directory base register.

	movl $0x10000, %eax
	movl %eax, %cr3
	
#### Switch to protected mode.

# Note that interrupts are still off.

# Point the GDTR to our GDT.  Protected mode requires a GDT.
# We need a data32 prefix to ensure that all 32 bits of the GDT
# descriptor are loaded (default is to load only 24 bits).

	data32 lgdt gdtdesc

# Then we turn on the following bits in CR0:
#    PE (Protect Enable): this turns on protected mode.
#    PG (Paging): turns on paging.
#    WP (Write Protect): if unset, ring 0 code ignores
#       write-protect bits in page tables (!).
#    EM (Emulation): forces floating-point instructions to trap.
#       We don't support floating point.
	
	movl %cr0, %eax
	orl $CR0_PE | CR0_PG | CR0_WP | CR0_EM, %eax
	movl %eax, %cr0
	
# We're now in protected mode in a 16-bit segment.  The CPU still has
# the real-mode code segment cached in %cs's segment descriptor.  We
# need to reload %cs, and the easiest way is to use a far jump.
# Because we're not in a 32-bit segment the data32 prefix is needed to
# jump to a 32-bit offset.

	data32 ljmp $SEL_KCSEG, $1f + LOADER_PHYS_BASE
	
# We're now in protected mode in a 32-bit segment.

	.code32

# Reload all the other segment registers and the stack pointer to
# point into our new GDT.

1:	movw $SEL_KDSEG, %ax
	movw %ax, %ds		
	movw %ax, %es		
	movw %ax, %fs		
	movw %ax, %gs		
	movw %ax, %ss
	movl $LOADER_PHYS_BASE + 0x30000, %esp

#### Load kernel starting at physical address LOADER_KERN_BASE by
#### frobbing the IDE controller directly.

	movl $1, %ebx
	movl $LOADER_KERN_BASE + LOADER_PHYS_BASE, %edi

# Disable interrupt delivery by IDE controller, because we will be
# polling for data.
# (If we don't do this, Bochs 2.2.6 will never deliver any IDE
# interrupt to us later after we reset the interrupt controller during
# boot, even if we also reset the IDE controller.)

	movw $0x3f6, %dx
	movb $0x02, %al
	outb %al, %dx
	
read_sector:

# Poll status register while controller busy.

	movl $0x1f7, %edx
1:	inb %dx, %al
	testb $0x80, %al
	jnz 1b

# Read a single sector.

	movl $0x1f2, %edx
	movb $1, %al
	outb %al, %dx

# Sector number to write in low 28 bits.
# LBA mode, device 0 in top 4 bits.

	movl %ebx, %eax
	andl $0x0fffffff, %eax
	orl $0xe0000000, %eax

# Dump %eax to ports 0x1f3...0x1f6.

	movl $4, %ecx
1:	incw %dx
	outb %al, %dx
	shrl $8, %eax
	loop 1b

# READ command to command register.

	incw %dx
	movb $0x20, %al
	outb %al, %dx

# Poll status register while controller busy.

1:	inb %dx, %al
	testb $0x80, %al
	jnz 1b

# Poll status register until data ready.

1:	inb %dx, %al
	testb $0x08, %al
	jz 1b

# Transfer sector.

	movl $256, %ecx
	movl $0x1f0, %edx
	rep insw

# Next sector.

	incl %ebx
	cmpl $KERNEL_LOAD_PAGES*8 + 1, %ebx
	jnz read_sector

#### Jump to kernel entry point.

	movl $LOADER_PHYS_BASE + LOADER_KERN_BASE, %eax
	call *%eax
	jmp panic

#### GDT

gdt:
	.quad 0x0000000000000000	# null seg
	.quad 0x00cf9a000000ffff	# code seg
	.quad 0x00cf92000000ffff        # data seg
	
gdtdesc:
	.word	0x17			# sizeof (gdt) - 1
	.long	gdt + LOADER_PHYS_BASE	# address gdt

#### Fatal error.
#### Print panic_message (with help from the BIOS) and spin.

panic:  .code16			# We only panic in real mode.
	movw $panic_message, %si
	movb $0xe, %ah
	subb %bh, %bh
1:	lodsb
	test %al, %al
2:	jz 2b			# Spin.
	int $0x10
	jmp 1b

panic_message:
	.ascii "Panic!"
	.byte 0

#### Physical memory size in 4 kB pages.
#### This is initialized by the loader and read by the kernel.
	.org LOADER_RAM_PGS - LOADER_BASE
ram_pgs:
	.long 0

#### Command-line arguments and their count.
#### This is written by the `pintos' utility and read by the kernel.
#### The loader itself does not do anything with the command line.
	.org LOADER_ARG_CNT - LOADER_BASE
arg_cnt:
	.long 0
	.org LOADER_ARGS - LOADER_BASE
args:
	.fill 0x80, 1, 0

#### Boot-sector signature.
#### The BIOS checks that this is set properly.
	.org LOADER_SIG - LOADER_BASE
	.word 0xaa55
