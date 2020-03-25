�����ϱ�����������ϸע�⣬����ʱ�����ޣ�������ֻ��δ˵���Ĳ��ֽ���ע�⡣
/*
 *  head.s contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00000000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory.
 */
.text
.globl _idt,_gdt,_pg_dir
_pg_dir:
startup_32:
//���뱣��ģʽ�󣬶μĴ����ͱ����ѡ���ӣ�ѡ���ӵĽṹ��
//		16		    2    1  0
//		---------------------------
//		|	����	  | TI	| RDL |
//		---------------------------
//����TI=0ʱ�Ǵ�GDT����������
	movl $0x10,%eax		//ѡ����=10��������Ϊ2����GDT�е���������������DATA������
	mov %ax,%ds			//ds es fs gs��ָ��DATA������
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
	lss _stack_start,%esp	//ds��SS��espָ��_stack_start����sched.c�ж��壩
	call setup_idt
	call setup_gdt
	movl $0x10,%eax		# reload all the segment registers
	mov %ax,%ds		# after changing gdt. CS was already
	mov %ax,%es		# reloaded in 'setup_gdt'
	mov %ax,%fs
	mov %ax,%gs
	lss _stack_start,%esp
	xorl %eax,%eax
1:	incl %eax		# check that A20 really IS enabled
//0X100000��1�״������A20�򿪣���ô0X000000��0X100000��ֵ��һ�������δ�򿪣�����һ����
	movl %eax,0x000000
	cmpl %eax,0x100000
	je 1b
	movl %cr0,%eax		# check math chip
	andl $0x80000011,%eax	# Save PG,ET,PE
	testl $0x10,%eax				//����Э��������ET=1��387��487����Э������
	jne 1f			# ET is set - 387 is present
	orl $4,%eax		# else set emulate bit	//ģ��Э������
1:	movl %eax,%cr0
	jmp after_page_tables

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It then loads
 *  idt. Everything that wants to install itself
 *  in the idt-table may do so themselves. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok. This routine will be over-
 *  written by the page tables.
 */
setup_idt:
//��������һ���ʽ��
//	7			      0
//	  -------------------------
//	0 |	    7-0ƫ��	      |
//	  -------------------------
//	1 |	   15-8ƫ��	      |
//	  -------------------------
//	2 |	  ѡ���ӵ�8λ	      |
//	  -------------------------
//	3 |	  ѡ���Ӹ�8λ	      |
//	  -------------------------
//	4 | 0 | 0 | 0 |   �ּ���   |
//	  -------------------------
//	5 | P |  DPL  | 0 |  ����  |
//	  -------------------------
//	6 |	   23-16ƫ��	      |
//	  -------------------------
//	7 |	   31-24ƫ��	      |
//	  -------------------------

	lea ignore_int,%edx
//	7			      0
//	  -------------------------
//	0 |	 ignore_int�ġ�     |
//	  -------------------------
//	1 |	  ƫ�Ƶĵ�16λ      |
//	  -------------------------
//	2 |��   ��     |     ��    |
//	  -------------------------
//	3 |	 �� 	������ ��    |
//	  -------------------------
//	4 | ���������� |   ������  |
//	  -------------------------
//	5 | �� ��������|    ��  �� |
//	  -------------------------
//	6 |	 ignore_int�ġ�     |
//	  -------------------------
//	7 |	 ƫ�Ƶĸ�16λ 	      |
//	  -------------------------
	movl $0x00080000,%eax
	movw %dx,%ax		/* selector = 0x0008 = cs */
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */

	lea _idt,%edi
	mov $256,%ecx
rp_sidt:
	movl %eax,(%edi)
	movl %edx,4(%edi)
	addl $8,%edi
	dec %ecx
	jne rp_sidt
	lidt idt_descr
	ret

/*
 *  setup_gdt
 *
 *  This routines sets up a new gdt and loads it.
 *  Only two entries are currently built, the same
 *  ones that were built in init.s. The routine
 *  is VERY complicated at two whole lines, so this
 *  rather long comment is certainly needed :-).
 *  This routine will beoverwritten by the page tables.
 */
setup_gdt:
	lgdt gdt_descr
	ret

.org 0x1000
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:		# This is not used yet, but if you
		# want to expand past 8 Mb, you'll have
		# to use it.

.org 0x4000
after_page_tables:				//���ں˿�ʼ����main������������ջ
	pushl $0		# These are the parameters to main :-)
	pushl $0
	pushl $0
	pushl $L6		# return address for main, if it decides to.
	pushl $_main
	jmp setup_paging
L6:
	jmp L6			# main should never return here, but
				# just in case, we know what happens.

/* This is the default interrupt "handler" :-) */
.align 2
ignore_int:
	incb 0xb8000+160		# put something on the screen
	movb $2,0xb8000+161		# so that we know something
	iret				# happened


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 8MB. The pager assumes that no illegal
 * addresses are produced (ie >4Mb on a 4Mb machine).
 *
 * NOTE! Although all physical memory should be identity
 * mapped by this routine, only the kernel page functions
 * use the >1Mb addresses directly. All "normal" functions
 * use just the lower 1Mb, or the local data space, which
 * will be mapped to some other place - mm keeps track of
 * that.
 *
 * For those with more memory than 8 Mb - tough luck. I've
 * not got it, why should you :-) The source is here. Change
 * it. (Seriously - it shouldn't be too difficult. Mostly
 * change some constants etc. I left it at 8Mb, as my machine
 * even cannot be extended past that (ok, but it was cheap :-)
 * I've tried to show which constants to change by having
 * some kind of marker at them (search for "8Mb"), but I
 * won't guarantee that's all :-( )
 */
.align 2
setup_paging:
	movl $1024*3,%ecx			//3��4K
	xorl %eax,%eax
	xorl %edi,%edi			/* pg_dir is at 0x000 */
	cld;rep;stosl				//stosl��eax�е�ֵ����edi��
//1024��ҳĿ¼�����ֻ������2����pg0  pg1)ҳ��ÿ��ҳ��Ѱַ4MB�����ҽ�ÿ��ҳ����ĵ�4λ��Ϊ7����˵��ҳ����Ч
	movl $pg0+7,_pg_dir		/* set present bit/user r/w */
	movl $pg1+7,_pg_dir+4		/*  --------- " " --------- */
	movl $pg1+4092,%edi
	movl $0x7ff007,%eax		/*  8Mb - 4096 + 7 (r/w user,p) */		
	std
//����Ĵ����Ƿ������PG0��PG1
1:	stosl			/* fill pages backwards - more efficient :-) */
	subl $0x1000,%eax
	jge 1b
	xorl %eax,%eax		/* pg_dir is at 0x0000 */
	movl %eax,%cr3		/* cr3 - page directory start */
	movl %cr0,%eax
	orl $0x80000000,%eax
	movl %eax,%cr0		/* set paging (PG) bit */
	ret			/* this also flushes prefetch-queue */

.align 2
.word 0
idt_descr:
	.word 256*8-1		# idt contains 256 entries
	.long _idt
.align 2
.word 0
gdt_descr:
	.word 256*8-1		# so does gdt (not that that's any
	.long _gdt		# magic number, but it works for me :^)

	.align 3
_idt:	.fill 256,8,0		# idt is uninitialized

_gdt:	.quad 0x0000000000000000	/* NULL descriptor */
//����һ������������˼�ǣ�
//	  7   		    0
//	  -----------------
//	0 |1|1|1|1|1|1|1|1|
//	  -----------------
//	1 |0|0|0|0|0|1|1|1|
//	  -----------------
//	2 |0|0|0|0|0|0|0|0|
//	  -----------------
//	3 |0|0|0|0|0|0|0|0|
//	  -----------------
//	4 |0|0|0|0|0|0|0|0|
//	  -----------------
//	5 |1|0|0|1|1|0|1|0|
//	  -----------------
//	6 |1|1|0|0|0|0|0|0|
//	  -----------------
//	7 |0|0|0|0|0|0|0|0|
//	  -----------------
//���ǿ���5�ֽڣ���0λ��0����ʾ�˶�δ����ȡ����δ�ù�����1λ��1����ʾ�˶��ǿ�д�ġ���2λ��0����ʾ�ǰ���ַ���ӷ����
//		��3λ��1����ʾ�˶��ǿ�ִ�еġ���4λ��1����ʾ�˶����û��Ρ���5��6λ����0��ʾ��Ȩ��Ϊ0����7λ��1��ʾ�˶��ڴ洢���С�
//���ǿ���6�ֽڣ���6λ��1��������32λ�ġ���7λ��1��˵���ν��ޱ�ʾ�ĳ�������4K�ֽ�Ϊ1ҳ��ҳ����Ŀ��
	.quad 0x00c09a00000007ff	/* 8Mb */
	.quad 0x00c09200000007ff	/* 8Mb */
	.quad 0x0000000000000000	/* TEMPORARY - don't use */
	.fill 252,8,0			/* space for LDT's and TSS's etc */
