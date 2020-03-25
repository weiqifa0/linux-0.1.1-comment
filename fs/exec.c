#include <errno.h>
#include <sys/stat.h>
#include <a.out.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/segment.h>

extern int sys_exit(int exit_code);
extern int sys_close(int fd);

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB !
 */
#define MAX_ARG_PAGES 32

#define cp_block(from,to) \
//������Ƕ������˼�ǣ�
//	movl	BLOCK_SIZE/4	%ecx
//	movl	from	%esi
//	movl	to	%edi
//	push	$0X10			//������
//	push	$0X17			//�����ݶ�
//	pop	%es			//��ESָ�����ݶ�
//	cld
//	rep	movsl			//ds:si--->es:di
//	pop	%es			//�ָ������
__asm__("pushl $0x10\n\t" \
	"pushl $0x17\n\t" \
	"pop %%es\n\t" \
	"cld\n\t" \
	"rep\n\t" \
	"movsl\n\t" \
	"pop %%es" \
	::"c" (BLOCK_SIZE/4),"S" (from),"D" (to) \
	:"cx","di","si")

/*
 * read_head() reads blocks 1-6 (not 0). Block 0 has already been
 * read for header information.
 */
int read_head(struct m_inode * inode,int blocks)
{
	struct buffer_head * bh;
	int count;

	if (blocks>6)
		blocks=6;
	for(count = 0 ; count<blocks ; count++) {
		if (!inode->i_zone[count+1])		//����ļ����пտ飬�Ͷ���һ��
			continue;
		if (!(bh=bread(inode->i_dev,inode->i_zone[count+1])))	//��5��ֱ�ӿ�
			return -1;
		cp_block(bh->b_data,count*BLOCK_SIZE);	//�����ݿ������ڴ�
		brelse(bh);
	}
	return 0;
}

int read_ind(int dev,int ind,long size,unsigned long offset)	//һ����ӿ�Ͷ�����ӿ鴦����
{
	struct buffer_head * ih, * bh;
	unsigned short * table,block;

	if (size<=0)
		panic("size<=0 in read_ind");
	if (size>512*BLOCK_SIZE)
		size=512*BLOCK_SIZE;
	if (!ind)
		return 0;
	if (!(ih=bread(dev,ind)))
		return -1;
	table = (unsigned short *) ih->b_data;
	while (size>0) {
		if (block=*(table++))
			if (!(bh=bread(dev,block))) {
				brelse(ih);
				return -1;
			} else {
				cp_block(bh->b_data,offset);
				brelse(bh);
			}
		size -= BLOCK_SIZE;
		offset += BLOCK_SIZE;
	}
	brelse(ih);
	return 0;
}

/*
 * read_area() reads an area into %fs:mem.	//��Ȼcp_block���������ݿ�����ES����FS��ES����ָ��0X17���ݶε�
 */
int read_area(struct m_inode * inode,long size)
{
	struct buffer_head * dind;
	unsigned short * table;
	int i,count;

	if ((i=read_head(inode,(size+BLOCK_SIZE-1)/BLOCK_SIZE)) ||
	    (size -= BLOCK_SIZE*6)<=0)
		return i;			//����ļ���С������6��ֱ�ӿ飬��ֱ�ӷ���
	if ((i=read_ind(inode->i_dev,inode->i_zone[7],size,BLOCK_SIZE*6)) ||
	    (size -= BLOCK_SIZE*512)<=0)
		return i;			//����һ����ӿ飬����ļ�С��û�ж�����ӿ飬����
	if (!(i=inode->i_zone[8]))
		return 0;
	if (!(dind = bread(inode->i_dev,i)))
		return -1;
	table = (unsigned short *) dind->b_data;
	for(count=0 ; count<512 ; count++)		//����������ӿ���FS
		if ((i=read_ind(inode->i_dev,*(table++),size,
		    BLOCK_SIZE*(518+count))) || (size -= BLOCK_SIZE*512)<=0)
			return i;
	panic("Impossibly long executable");
}

/*
 * create_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
static unsigned long * create_tables(char * p,int argc,int envc)	//�������ͻ��������ֱ𿽱���argV��envP�У������ͽ��������������û��ڴ�
{
	unsigned long *argv,*envp;
	unsigned long * sp;

	sp = (unsigned long *) (0xfffffffc & (unsigned long) p);
	sp -= envc+1;
	envp = sp;
	sp -= argc+1;
	argv = sp;
	put_fs_long((unsigned long)envp,--sp);
	put_fs_long((unsigned long)argv,--sp);
	put_fs_long((unsigned long)argc,--sp);
	while (argc-->0) {
		put_fs_long((unsigned long) p,argv++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,argv);
	while (envc-->0) {
		put_fs_long((unsigned long) p,envp++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,envp);
	return sp;
}

/*
 * count() counts the number of arguments/envelopes
 */
static int count(char ** argv)	//��������������������ַ�����
{
	int i=0;
	char ** tmp;

	if (tmp = argv)
		while (get_fs_long((unsigned long *) (tmp++)))
			i++;

	return i;
}

/*
 * 'copy_string()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 */
static unsigned long copy_strings(int argc,char ** argv,unsigned long *page,
		unsigned long p)
{
	int len,i;
	char *tmp;

	while (argc-- > 0) {
		if (!(tmp = (char *)get_fs_long(((unsigned long *) argv)+argc)))
			panic("argc is wrong");
		len=0;		/* remember zero-padding */
		do {
			len++;
		} while (get_fs_byte(tmp++));
		if (p-len < 0)		/* this shouldn't happen - 128kB */
			return 0;
		i = ((unsigned) (p-len)) >> 12;		//ȡ�ó���ɷ��������ҳ��
		while (i<MAX_ARG_PAGES && !page[i]) {
			if (!(page[i]=get_free_page()))	//Ϊ����ҳ�ŷ���һ�������ڴ�
				return 0;
			i++;
		}
		do {
			--p;
			if (!page[p/PAGE_SIZE])
				panic("nonexistent page in exec.c");
			((char *) page[p/PAGE_SIZE])[p%PAGE_SIZE] =
				get_fs_byte(--tmp);		//����������д��ó��������ҳ��
		} while (--len);
	}
	return p;
}

static unsigned long change_ldt(unsigned long text_size,unsigned long * page)
{
	unsigned long code_limit,data_limit,code_base,data_base;
	int i;

	code_limit = text_size+PAGE_SIZE -1;
	code_limit &= 0xFFFFF000;
	data_limit = 0x4000000;
	code_base = get_base(current->ldt[1]);	//ȡ��ǰ���̵�IDT�Ļ���ַ
	data_base = code_base;
	set_base(current->ldt[1],code_base);	//�������õ�ǰ���̵�IDT�Ĵ������ַ
	set_limit(current->ldt[1],code_limit);	//���ô���ν���
	set_base(current->ldt[2],data_base);	//�����������ݶλ���ַ
	set_limit(current->ldt[2],data_limit);	//���ݶν�������
/* make sure fs points to the NEW data segment */
	__asm__("pushl $0x17\n\tpop %%fs"::);	//��FSָ�����ݶ�
	data_base += data_limit;
	for (i=MAX_ARG_PAGES-1 ; i>=0 ; i--) {	//���ĸ�ҳ���ѱ������������ڴ棬��������get_free_page��������������ˣ��ͰѸ�ҳ�ĵ�ַ�Ǽ���page_table�����������Ϊ�˽����л����������Ǹ�ҳ�ŵ���������ҳ
		data_base -= PAGE_SIZE;
		if (page[i])
			put_page(page[i],data_base);
	}
	return data_limit;
}

/*
 * 'do_execve()' executes a new program.
 */
int do_execve(unsigned long * eip,long tmp,char * filename,
	char ** argv, char ** envp)
{
	struct m_inode * inode;
	struct buffer_head * bh;
	struct exec ex;				//��a.out.h�ж���
	unsigned long page[MAX_ARG_PAGES];
	int i,argc,envc;
	unsigned long p;

	if ((0xffff & eip[1]) != 0x000f)		//�Ƿ�Ϊ����ԱMODE
		panic("execve called from supervisor mode");
	for (i=0 ; i<MAX_ARG_PAGES ; i++)	/* clear page-table */
		page[i]=0;
	if (!(inode=namei(filename)))		/* get executables inode */
		return -ENOENT;
	if (!S_ISREG(inode->i_mode)) {	/* must be regular file */
		iput(inode);
		return -EACCES;
	}
	i = inode->i_mode;
	if (current->uid && current->euid) {	//�����޸��´������̵�i_mode
		if (current->euid == inode->i_uid)
			i >>= 6;
		else if (current->egid == inode->i_gid)
			i >>= 3;
	} else if (i & 0111)
		i=1;
	if (!(i & 1)) {				//����ǲ���ִ���ļ�����ô���ͷŸ��ļ���INODE
		iput(inode);
		return -ENOEXEC;
	}
	if (!(bh = bread(inode->i_dev,inode->i_zone[0]))) {	//���ÿ�ִ���ļ��ĵ�һֱ�ӿ���뻺����
		iput(inode);
		return -EACCES;
	}
	ex = *((struct exec *) bh->b_data);	/* read exec-header */	//ִ���ļ��ĵ�һ��ĵ�һ�������ǿ�ִ���ļ�ͷ
	brelse(bh);
	if (N_MAGIC(ex) != ZMAGIC || ex.a_trsize || ex.a_drsize ||
		ex.a_text+ex.a_data+ex.a_bss>0x3000000 ||				//0X3000000=48M
		inode->i_size < ex.a_text+ex.a_data+ex.a_syms+N_TXTOFF(ex)) {	//����ִ���ļ�ͷ�Ƿ�Ϊ�Ϸ��Ŀ�ִ���ļ�
		iput(inode);
		return -ENOEXEC;
	}
	if (N_TXTOFF(ex) != BLOCK_SIZE)
		panic("N_TXTOFF != BLOCK_SIZE. See a.out.h.");
	argc = count(argv);
	envc = count(envp);
	p = copy_strings(envc,envp,page,PAGE_SIZE*MAX_ARG_PAGES-4);	//����������д����������ҳ��
	p = copy_strings(argc,argv,page,p);				//������д�뻷������֮ǰ
	if (!p) {					//����Ҫ���еĴ���
		for (i=0 ; i<MAX_ARG_PAGES ; i++)
			free_page(page[i]);
		iput(inode);
		return -1;
	}
/* OK, This is the point of no return */
	for (i=0 ; i<32 ; i++)
		current->sig_fn[i] = NULL;		//��ʼ��sig_fnΪ��
	for (i=0 ; i<NR_OPEN ; i++)
		if ((current->close_on_exec>>i)&1)
			sys_close(i);
	current->close_on_exec = 0;
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));		//�ͷŴ����ҳ��
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));		//�ͷ����ݶ�ҳ��
	if (last_task_used_math == current)					//Ϊ���̵�������ʼ��
		last_task_used_math = NULL;
	current->used_math = 0;
	p += change_ldt(ex.a_text,page)-MAX_ARG_PAGES*PAGE_SIZE;
	p = (unsigned long) create_tables((char *)p,argc,envc);
	current->brk = ex.a_bss +
		(current->end_data = ex.a_data +
		(current->end_code = ex.a_text));			//����������̵ĳ��ȣ����޸ĵ�ǰ���̵�����
	current->start_stack = p & 0xfffff000;			//ָ���ջҳ��
	i = read_area(inode,ex.a_text+ex.a_data);			//���ļ������ĺ����ݶ�����FS
	iput(inode);
	if (i<0)
		sys_exit(-1);
	i = ex.a_text+ex.a_data;
	while (i&0xfff)				//��0����öΣ�ʹ������
		put_fs_byte(0,(char *) (i++));
	eip[0] = ex.a_entry;		/* eip, magic happens :-) */
	eip[3] = p;			/* stack pointer */
	return 0;
}
