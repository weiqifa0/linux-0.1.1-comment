#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

/*
 * This code handles all hd-interrupts, and read/write requests to
 * the hard-disk. It is relatively straigthforward (not obvious maybe,
 * but interrupts never are), while still being efficient, and never
 * disabling interrupts (except to overcome possible race-condition).
 * The elevator block-seek algorithm doesn't need to disable interrupts
 * due to clever programming.
 */

/* Max read/write errors/sector */
#define MAX_ERRORS	5
#define MAX_HD		2
#define NR_REQUEST	32

/*
 *  This struct defines the HD's and their types.
 *  Currently defined for CP3044's, ie a modified
 *  type 17.
 */
static struct hd_i_struct{
	int head,sect,cyl,wpcom,lzone,ctl;
	} hd_info[]= { HD_TYPE };

#define NR_HD ((sizeof (hd_info))/(sizeof (struct hd_i_struct)))

static struct hd_struct {
	long start_sect;
	long nr_sects;
} hd[5*MAX_HD]={{0,0},};			//һ��Ӳ�̿ɷ�5����������2��Ӳ��

static struct hd_request {
	int hd;		/* -1 if no request */
	int nsector;
	int sector;
	int head;
	int cyl;
	int cmd;
	int errors;
	struct buffer_head * bh;
	struct hd_request * next;
} request[NR_REQUEST];

#define IN_ORDER(s1,s2) \
((s1)->hd<(s2)->hd || (s1)->hd==(s2)->hd && \
((s1)->cyl<(s2)->cyl || (s1)->cyl==(s2)->cyl && \
((s1)->head<(s2)->head || (s1)->head==(s2)->head && \
((s1)->sector<(s2)->sector))))

static struct hd_request * this_request = NULL;

static int sorting=0;

static void do_request(void);
static void reset_controller(void);
static void rw_abs_hd(int rw,unsigned int nr,unsigned int sec,unsigned int head,
	unsigned int cyl,struct buffer_head * bh);
void hd_init(void);

#define port_read(port,buf,nr) \
//�������Ƕ������˼�ǣ�
//	movl	port	%edx
//	movl	buf	%edi
//	movl	nr	%ecx
//	cld
//	rep	insw		//��edxָ����port�����ֵ�es��ediָ����buf��
__asm__("cld;rep;insw"::"d" (port),"D" (buf),"c" (nr):"cx","di")

#define port_write(port,buf,nr) \
__asm__("cld;rep;outsw"::"d" (port),"S" (buf),"c" (nr):"cx","si")	//����outsw�Ǵ�ds��siָ����buf�д����ֵ�edxָ����port

extern void hd_interrupt(void);			//��system_call.S�ж���

static struct task_struct * wait_for_request=NULL;

static inline void lock_buffer(struct buffer_head * bh)
{
	if (bh->b_lock)				//�˻������Ƿ�����
		printk("hd.c: buffer multiply locked\n");
	bh->b_lock=1;
}

static inline void unlock_buffer(struct buffer_head * bh)
{
	if (!bh->b_lock)
		printk("hd.c: free buffer being unlocked\n");
	bh->b_lock=0;
	wake_up(&bh->b_wait);
}

static inline void wait_on_buffer(struct buffer_head * bh)
{
	cli();
	while (bh->b_lock)
		sleep_on(&bh->b_wait);		//����ȴ�����
	sti();
}

void rw_hd(int rw, struct buffer_head * bh)	//�������ʹ���֮�佻������
{
	unsigned int block,dev;
	unsigned int sec,head,cyl;

	block = bh->b_blocknr << 1;			//Ĭ��1��=1024�ֽڣ���������
	dev = MINOR(bh->b_dev);
	if (dev >= 5*NR_HD || block+2 > hd[dev].nr_sects)	//�Ƿ񳬹��˷������˷����Ƿ�����
		return;
	block += hd[dev].start_sect;		//�˷����Ŀ�ʼ����+block������Ӳ���ж���ʼ��λ��
	dev /= 5;					//�������ĸ�Ӳ��
	__asm__("divl %4":"=a" (block),"=d" (sec):"0" (block),"1" (0),	
		"r" (hd_info[dev].sect));					//ȷ��������
	__asm__("divl %4":"=a" (cyl),"=d" (head):"0" (block),"1" (0),
		"r" (hd_info[dev].head));					//ȷ����ͷ��
	rw_abs_hd(rw,dev,sec+1,head,cyl,bh);	//ʵ��д�̺���
}

/* This may be used only once, enforced by 'static int callable' */
int sys_setup(void)
{
	static int callable = 1;
	int i,drive;
	struct partition *p;

	if (!callable)
		return -1;
	callable = 0;
	for (drive=0 ; drive<NR_HD ; drive++) {
		rw_abs_hd(READ,drive,1,0,0,(struct buffer_head *) start_buffer);	//��ȡ��������512�ֽڵ�start_buffer��
		if (!start_buffer->b_uptodate) {
			printk("Unable to read partition table of drive %d\n\r",
				drive);
			panic("");
		}
		if (start_buffer->b_data[510] != 0x55 || (unsigned char)
		    start_buffer->b_data[511] != 0xAA) {				//�������������Ƿ�Ϸ����Ϸ�ӦΪ55AA
			printk("Bad partition table on drive %d\n\r",drive);
			panic("");
		}
		p = 0x1BE + (void *)start_buffer->b_data;		//ָ��ָ�������Ϣ�Ŀ�ʼλ��
		for (i=1;i<5;i++,p++) {				//���������Ϣ
			hd[i+5*drive].start_sect = p->start_sect;
			hd[i+5*drive].nr_sects = p->nr_sects;
		}
	}
	printk("Partition table%s ok.\n\r",(NR_HD>1)?"s":"");
	mount_root();				//��super.c�ж���
	return (0);
}

/*
 * This is the pointer to a routine to be executed at every hd-interrupt.
 * Interesting way of doing things, but should be rather practical.
 */
void (*do_hd)(void) = NULL;

static int controller_ready(void)
{
	int retries=1000;

	while (--retries && (inb(HD_STATUS)&0xc0)!=0x40);	//0x40�ǵ�����λ��1Ϊ������׼���ã�0x1f7�˿��Ƕ�ȡӲ��״̬
	return (retries);
}

static int win_result(void)
{
	int i=inb(HD_STATUS);	//��ȡӲ��״̬

	if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))
		== (READY_STAT | SEEK_STAT))
		return(0); /* ok */
	if (i&1) i=inb(HD_ERROR);	//0x1f1��Ӳ�̴���Ĵ����������һ�������Ƿ����
	return (1);
}

static void hd_out(unsigned int drive,unsigned int nsect,unsigned int sect,
		unsigned int head,unsigned int cyl,unsigned int cmd,
		void (*intr_addr)(void))
{
	register int port asm("dx");

	if (drive>1 || head>15)	//ֻ֧��1��Ӳ�̺�14��ͷ
		panic("Trying to write bad sector");
	if (!controller_ready())
		panic("HD controller not ready");
	do_hd = intr_addr;		//Ӳ��д�������ָ��
	outb(_CTL,HD_CMD);		//0x3f6��Ӳ�̿��ƼĴ�������0Ϊ������Ϊ��ͨ����
	port=HD_DATA;			//0x1f0Ӳ�����ݼĴ���
	outb_p(_WPCOM,++port);	//0x1f1��дԤ�������ڴӴ���300X4����ʼд
	outb_p(nsect,++port);	//0x1f2��Ӳ�����������Ĵ�������Ҫ���͵�����������üĴ���
	outb_p(sect,++port);		//0x1f3��Ӳ�������żĴ�������¼��ǰ��������
	outb_p(cyl,++port);		//0x1f4Ӳ�̴����ͼĴ�����������ʼ�����ŵĵ�8�ֽ�
	outb_p(cyl>>8,++port);	//0x1f5Ӳ�̴����߼Ĵ���
	outb_p(0xA0|(drive<<4)|head,++port);	//0x1f6Ӳ���������ʹ�ͷ�Ĵ���������ѡ�����̣���4λΪ1010������4λΪ��ͷ��
	outb(cmd,++port);		//0x1f7Ӳ����������Ĵ�������Ҫ��Ӳ�̷�������
}

static int drive_busy(void)
{
	unsigned int i;

	for (i = 0; i < 100000; i++)	//ѭ����ֱ��������ΪREADY_STAT��ʱ
		if (READY_STAT == (inb(HD_STATUS) & (BUSY_STAT | READY_STAT)))
			break;
	i = inb(HD_STATUS);			//�ٶ�Ӳ��״̬
	i &= BUSY_STAT | READY_STAT | SEEK_STAT;
	if (i == READY_STAT | SEEK_STAT)	//������ڶ�״̬��Ѱ��״̬��OK
		return(0);
	printk("HD controller times out\n\r");
	return(1);
}

static void reset_controller(void)
{
	int	i;

	outb(4,HD_CMD);		//0x3f6��Ӳ�����������ƼĴ������ڶ�λ��1Ϊ����������
	for(i = 0; i < 1000; i++) nop();	//�ȴ�һ���
	outb(0,HD_CMD);		//ʹ������������ͨ״̬
	for(i = 0; i < 10000 && drive_busy(); i++) /* nothing */;
	if (drive_busy())
		printk("HD-controller still busy\n\r");
	if((i = inb(ERR_STAT)) != 1)
		printk("HD-controller reset failed: %02x\n\r",i);
}

static void reset_hd(int nr)
{
	reset_controller();
	hd_out(nr,_SECT,_SECT,_HEAD-1,_CYL,WIN_SPECIFY,&do_request);	//do_request�����´����������
}

void unexpected_hd_interrupt(void)
{
	panic("Unexpected HD interrupt\n\r");
}

static void bad_rw_intr(void)
{
	int i = this_request->hd;

	if (this_request->errors++ >= MAX_ERRORS) {	//������󳬹���Ч�����������������������
		this_request->bh->b_uptodate = 0;
		unlock_buffer(this_request->bh);
		wake_up(&wait_for_request);
		this_request->hd = -1;
		this_request=this_request->next;
	}
	reset_hd(i);
}

static void read_intr(void)
{
	if (win_result()) {
		bad_rw_intr();
		return;
	}
	port_read(HD_DATA,this_request->bh->b_data+
		512*(this_request->nsector&1),256);	//�Ǵ�b_dataָ��λ�ö�512�ֽ�
	this_request->errors = 0;
	if (--this_request->nsector)		//�������Ҫ�������ݣ�ֱ�ӷ��أ��������������ɾ��������
		return;
//����6�д�����ָ���ݴ�����ϣ��ʹ����������ɾ��������
	this_request->bh->b_uptodate = 1;
	this_request->bh->b_dirt = 0;
	wake_up(&wait_for_request);
	unlock_buffer(this_request->bh);
	this_request->hd = -1;
	this_request=this_request->next;
	do_request();				//������������
}

static void write_intr(void)
{
	if (win_result()) {
		bad_rw_intr();		//����д����������������ټ�����������
		return;
	}
	if (--this_request->nsector) {
		port_write(HD_DATA,this_request->bh->b_data+512,256);	//��Ӳ�����ݼĴ���д����
		return;
	}
	this_request->bh->b_uptodate = 1;
	this_request->bh->b_dirt = 0;
	wake_up(&wait_for_request);
	unlock_buffer(this_request->bh);
	this_request->hd = -1;
	this_request=this_request->next;
	do_request();				//������һ������
}

static void do_request(void)
{
	int i,r;

	if (sorting)
		return;
	if (!this_request) {
		do_hd=NULL;
		return;
	}
	if (this_request->cmd == WIN_WRITE) {
		hd_out(this_request->hd,this_request->nsector,this_request->
			sector,this_request->head,this_request->cyl,
			this_request->cmd,&write_intr);
		for(i=0 ; i<3000 && !(r=inb_p(HD_STATUS)&DRQ_STAT) ; i++)	//ѭ����ֱ��������ָʾ׼����
			/* nothing */ ;
		if (!r) {					//�����ʱ��������δ׼���ã������������
			reset_hd(this_request->hd);
			return;
		}
		port_write(HD_DATA,this_request->bh->b_data+
			512*(this_request->nsector&1),256);		//��������
	} else if (this_request->cmd == WIN_READ) {
		hd_out(this_request->hd,this_request->nsector,this_request->
			sector,this_request->head,this_request->cyl,
			this_request->cmd,&read_intr);
	} else
		panic("unknown hd-command");
}

/*
 * add-request adds a request to the linked list.
 * It sets the 'sorting'-variable when doing something
 * that interrupts shouldn't touch.
 */
static void add_request(struct hd_request * req)
{
	struct hd_request * tmp;

	if (req->nsector != 2)
		panic("nsector!=2 not implemented");
/*
 * Not to mess up the linked lists, we never touch the two first
 * entries (not this_request, as it is used by current interrups,
 * and not this_request->next, as it can be assigned to this_request).
 * This is not too high a price to pay for the ability of not
 * disabling interrupts.
 */
	sorting=1;
	if (!(tmp=this_request))
		this_request=req;
	else {
		if (!(tmp->next))
			tmp->next=req;
		else {
			tmp=tmp->next;
			for ( ; tmp->next ; tmp=tmp->next)
				if ((IN_ORDER(tmp,req) ||
				    !IN_ORDER(tmp,tmp->next)) &&
				    IN_ORDER(req,tmp->next))
					break;
			req->next=tmp->next;
			tmp->next=req;
		}
	}
	sorting=0;
/*
 * NOTE! As a result of sorting, the interrupts may have died down,
 * as they aren't redone due to locking with sorting=1. They might
 * also never have started, if this is the first request in the queue,
 * so we restart them if necessary.
 */
	if (!do_hd)
		do_request();
}

void rw_abs_hd(int rw,unsigned int nr,unsigned int sec,unsigned int head,
	unsigned int cyl,struct buffer_head * bh)					//����Ӳ�̲��������̶�д
{
	struct hd_request * req;

	if (rw!=READ && rw!=WRITE)
		panic("Bad hd command, must be R/W");
	lock_buffer(bh);
repeat:
	for (req=0+request ; req<NR_REQUEST+request ; req++)
		if (req->hd<0)					//hd=-1��ʾû������
			break;
	if (req==NR_REQUEST+request) {				//���ܳ�������������
		sleep_on(&wait_for_request);
		goto repeat;
	}
	req->hd=nr;
	req->nsector=2;						//==1024�ֽ�
	req->sector=sec;
	req->head=head;
	req->cyl=cyl;
	req->cmd = ((rw==READ)?WIN_READ:WIN_WRITE);
	req->bh=bh;
	req->errors=0;
	req->next=NULL;
	add_request(req);
	wait_on_buffer(bh);						//����ȴ�
}

void hd_init(void)
{
	int i;

	for (i=0 ; i<NR_REQUEST ; i++) {				//��ʼ���������
		request[i].hd = -1;
		request[i].next = NULL;
	}
	for (i=0 ; i<NR_HD ; i++) {					//��ʼ��Ӳ�̷���
		hd[i*5].start_sect = 0;
		hd[i*5].nr_sects = hd_info[i].head*
				hd_info[i].sect*hd_info[i].cyl;
	}
	set_trap_gate(0x2E,&hd_interrupt);				//�Ǽ�Ӳ���жϴ�������жϺ�Ϊ0x2E
	outb_p(inb_p(0x21)&0xfb,0x21);				//����IRQ2���жϿ�����1��
	outb(inb_p(0xA1)&0xbf,0xA1);				//����IRQ14��Ӳ�̿�����
}
