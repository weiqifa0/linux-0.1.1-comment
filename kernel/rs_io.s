/*
 *	rs_io.s
 *
 * This module implements the rs232 io interrupts.
 */

.text
.globl _rs1_interrupt,_rs2_interrupt

size	= 1024				/* must be power of two !
					   and must match the value
					   in tty_io.c!!! */

/* these are the offsets into the read/write buffer structures */
rs_addr = 0
head = 4
tail = 8
proc_list = 12
buf = 16

startup	= 256		/* chars left in write queue when we restart it */

/*
 * These are the actual interrupt routines. They look where
 * the interrupt is coming from, and take appropriate action.
 */
.align 2
_rs1_interrupt:
	pushl $_table_list+8		//������tty_io.c�ж����table_list�ṹ�Ļ���ַ+8����ƫ����8���ֽڣ�����table_list[1].read_q�ṹ���׵�ַ��������ṹ�ĵ�һ����Ա��data��0x3f8�������о��ǽ�����ջ
	jmp rs_int
.align 2
_rs2_interrupt:
	pushl $_table_list+16
rs_int:
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	push %es
	push %ds		/* as this is an interrupt, we cannot */
	pushl $0x10		/* know that bs is ok. Load it */
	pop %ds			//��ds��Ϊ0X10
	pushl $0x10
	pop %es			//��es��Ϊ0X10
	movl 24(%esp),%edx		//��ָ��ָ��$_table_list+8�ĵ�ַ0x3F8��esp+24��
	movl (%edx),%edx		//����ַ�ϵ�ֵ����edx��ֵΪ0x3F8
	movl rs_addr(%edx),%edx
	addl $2,%edx		/* interrupt ident. reg */		//=0X3FA
rep_int:
	xorl %eax,%eax
	inb %dx,%al			//UART�Ĵ���2���жϱ�ʶ�Ĵ�����
	testb $1,%al			//���0λΪ1����ô���жϹ��𣬽���
	jne end
	cmpb $6,%al		/* this shouldn't happen, but ... */	//���al>6�����ǵ�3λ��Ϊ1�������ַ���ʱ������
	ja end
	movl 24(%esp),%ecx		//��$_table_list+8��table_list[1].read_q���Ļ���ַ����ecx
	pushl %edx
	subl $2,%edx			//0X3F8
	call jmp_table(,%eax,2)		/* NOTE! not *4, bit0 is 0 already */
	popl %edx
	jmp rep_int			//ѭ������ͣ�ļ���Ƿ����жϷ���
end:	movb $0x20,%al		//��˿�20��������20�ǽ����ж�
	outb %al,$0x20		/* EOI */
	pop %ds
	pop %es
	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	addl $4,%esp		# jump over _table_list entry
	iret

jmp_table:
	.long modem_status,write_char,read_char,line_status

.align 2
modem_status:				//��ȡ�Ĵ���״̬ʹ��0X3F8+6�ļĴ�����0��3λ����Ϊ0
	addl $6,%edx		/* clear intr by reading modem status reg */
	inb %dx,%al
	ret

.align 2
line_status:				//��ȡ�Ĵ���״̬��ʹ��0X3F8+5�ļĴ�������Ϊ60H����ʾû�д��󣬴��ͻ�����Ϊ��
	addl $5,%edx		/* clear intr by reading line status reg. */
	inb %dx,%al
	ret

.align 2
read_char:
	inb %dx,%al			//0X3F8�Ĵ��������ǻ�ý��ջ����������ֽ�
	movl %ecx,%edx		//��ʱecx����table_list[1].read_q�ĵ�ַ
	subl $_table_list,%edx	//edx=8
	shrl $3,%edx			//edx=1
	movl (%ecx),%ecx		# read-queue	//��table_list[1].read_q�ĵ�ַ��ecx���е�ֵ����ecx��dataֵΪ3F8
	movl head(%ecx),%ebx		//��table_list[1].read_q�ṹ�еĵ�2����Ա��ַ��head���е�ֵ����ebx��Ϊ0
	movb %al,buf(%ecx,%ebx)	//���ַ����ݷ���ṹ�е�char
	incl %ebx			//ebx=1
	andl $size-1,%ebx		//andl	3FF	%ebx
	cmpl tail(%ecx),%ebx		//��һ��ѭ���ڼ�tail��%ecx��Ϊ0
	je 1f
	movl %ebx,head(%ecx)		//���ṹ��Աhead��ֵ��Ϊ1
	pushl %edx
	call _do_tty_interrupt
	addl $4,%esp
1:	ret

.align 2
write_char:
	movl 4(%ecx),%ecx		# write-queue		//ecx=table_list[1].write_q
	movl head(%ecx),%ebx		//ebx=table_list[1].write_q�ṹ�е�head��Ա=0
	subl tail(%ecx),%ebx		//ָ��table_list[1].write_q�ṹ�е�tail��Ա=0
	andl $size-1,%ebx		# nr chars in queue
	je write_buffer_empty
	cmpl $startup,%ebx
	ja 1f
	movl proc_list(%ecx),%ebx	# wake up sleeping process
	testl %ebx,%ebx			# is there any?
	je 1f				//ebx=0ʱ��ת
	movl $0,(%ebx)
1:	movl tail(%ecx),%ebx
	movb buf(%ecx,%ebx),%al	//�����������ֽ���������al��ʵ��д���ݵĴ���
	outb %al,%dx
	incl %ebx			//����table_list[1].write_q�е�״̬
	andl $size-1,%ebx
	movl %ebx,tail(%ecx)
	cmpl head(%ecx),%ebx
	je write_buffer_empty
	ret
.align 2
write_buffer_empty:
	movl proc_list(%ecx),%ebx	# wake up sleeping process
	testl %ebx,%ebx			# is there any?
	je 1f
	movl $0,(%ebx)
1:	incl %edx			//0X3F9���жϿ��żĴ�������4λ����
	inb %dx,%al
	jmp 1f
1:	jmp 1f
1:	andb $0xd,%al		/* disable transmit interrupt */
	outb %al,%dx
	ret
