/*
 *	serial.c
 *
 * This module implements the rs232 io functions
 *	void rs_write(struct tty_struct * queue);
 *	void rs_init(void);
 * and all interrupts pertaining to serial IO.
 */

#include <linux/tty.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>

#define WAKEUP_CHARS (TTY_BUF_SIZE/4)

extern void rs1_interrupt(void);
extern void rs2_interrupt(void);

static void init(int port)
{
	outb_p(0x80,port+3);	/* set DLAB of line control reg */		//�߿��ƼĴ���������7λ��1�ǿ��ų���������������ַλ
	outb_p(0x30,port);	/* LS of divisor (48 -> 2400 bps */	//����������Ϊ2400������Чλ��
	outb_p(0x00,port+1);	/* MS of divisor */				//�������ʵ������Чλ������üĴ���
	outb_p(0x03,port+3);	/* reset DLAB */				//��DLAB��Ϊ0������Ϊ8λ��ͨ����8λ��
	outb_p(0x0b,port+4);	/* set DTR,RTS, OUT_2 */			//���������ն�׼�����ߣ�DTR����״̬�������������ߣ�RTS����״̬��out_2���ڿ����ж�����
	outb_p(0x0d,port+1);	/* enable all intrs but writes */
	(void)inb(port);	/* read data port to reset things (?) */	//��ȡ���ջ����������ֽ�
}

void rs_init(void)
{
	set_intr_gate(0x24,rs1_interrupt);			//���пڶ˿�3F8���жϺ���0x24
	set_intr_gate(0x23,rs2_interrupt);			//���пڶ˿�2F8���жϺ���0x23
	init(tty_table[1].read_q.data);			//��ʼ��2�����ж˿�
	init(tty_table[2].read_q.data);
	outb(inb_p(0x21)&0xE7,0x21);			//�����жϿ��������ж�������0--7�Ĳ���
}

/*
 * This routine gets called when tty_write has put something into
 * the write_queue. It must check wheter the queue is empty, and
 * set the interrupt register accordingly
 *
 *	void _rs_write(struct tty_struct * tty);
 */
void rs_write(struct tty_struct * tty)
{
	cli();
	if (!EMPTY(tty->write_q))
		outb(inb_p(tty->write_q.data+1)|0x02,tty->write_q.data+1);	//���Ĵ����ĵ�2λ������λ����1�����Ŵ����ж�
	sti();
}
