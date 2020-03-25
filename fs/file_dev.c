#include <errno.h>
#include <fcntl.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

int file_read(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	int left,chars,nr;
	struct buffer_head * bh;

	if ((left=count)<=0)
		return 0;
	while (left) {
		if (nr = bmap(inode,(filp->f_pos)/BLOCK_SIZE)) {	//ȡ��Ҫ��ȡ���ļ����
			if (!(bh=bread(inode->i_dev,nr)))
				break;
		} else
			bh = NULL;
		nr = filp->f_pos % BLOCK_SIZE;			//ȡ���ڸÿ��е�λ��
		chars = MIN( BLOCK_SIZE-nr , left );
		filp->f_pos += chars;		//��������2��������Ϊ����һ����׼��
		left -= chars;
		if (bh) {
			char * p = nr + bh->b_data;		//ָ��ָ����е�������
			while (chars-->0)
				put_fs_byte(*(p++),buf++);	//��ȡ���ݵ�ָ��������buf
			brelse(bh);			//�ͷŻ�����
		} else {
			while (chars-->0)		//���ļ�β
				put_fs_byte(0,buf++);
		}
	}
	inode->i_atime = CURRENT_TIME;
	return (count-left)?(count-left):-ERROR;		//���ض�ȡ���ַ���
}

int file_write(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	off_t pos;
	int block,c;
	struct buffer_head * bh;
	char * p;
	int i=0;

/*
 * ok, append may not work when many processes are writing at the same time
 * but so what. That way leads to madness anyway.
 */
	if (filp->f_flags & O_APPEND)		//������������ļ�β��������
		pos = inode->i_size;			//λ��ָ���ļ�β
	else
		pos = filp->f_pos;			//λ��ָ��ǰ���λ��
	while (i<count) {
		if (!(block = create_block(inode,pos/BLOCK_SIZE)))	//���б�Ҫ�������д����¿�
			break;
		if (!(bh=bread(inode->i_dev,block)))		//��ȡ�ÿ�
			break;
		c = pos % BLOCK_SIZE;		//ȡ���ڸÿ��е�λ��
		p = c + bh->b_data;			//ָ��ָ���λ��
		bh->b_dirt = 1;			//��bh����д��
		c = BLOCK_SIZE-c;			//�ڸÿ��л�����д�����ַ�
		if (c > count-i) c = count-i;
		pos += c;				//���¼���λ��
		if (pos > inode->i_size) {		//����ļ�����
			inode->i_size = pos;		//���������ļ��ߴ�
			inode->i_dirt = 1;
		}
		i += c;
		while (c-->0)
			*(p++) = get_fs_byte(buf++);	//��buf�е�����д��ÿ��������
		brelse(bh);
	}
	inode->i_mtime = CURRENT_TIME;
	if (!(filp->f_flags & O_APPEND)) {
		filp->f_pos = pos;
		inode->i_ctime = CURRENT_TIME;
	}
	return (i?i:-1);
}
