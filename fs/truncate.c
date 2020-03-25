#include <linux/sched.h>

#include <sys/stat.h>

static void free_ind(int dev,int block)		//�ͷ�inode��һ�����ӳ��
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;

	if (!block)
		return;
	if (bh=bread(dev,block)) {
		p = (unsigned short *) bh->b_data;
		for (i=0;i<512;i++,p++)
			if (*p)
				free_block(dev,*p);
		brelse(bh);
	}
	free_block(dev,block);
}

static void free_dind(int dev,int block)		//�ͷ�inode�Ķ������ӳ��
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;

	if (!block)
		return;
	if (bh=bread(dev,block)) {
		p = (unsigned short *) bh->b_data;
		for (i=0;i<512;i++,p++)
			if (*p)
				free_ind(dev,*p);
		brelse(bh);
	}
	free_block(dev,block);
}

void truncate(struct m_inode * inode)		//�ͷŴ�inode��ֱ��ӳ�估һ���Ͷ������ӳ��
{
	int i;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))	//������Ŀ¼
		return;
	for (i=0;i<7;i++)				//�ͷ�ֱ��ӳ�䣬i_zone[0]~i_zone[6]��ֱ�ӿ�ӳ��
		if (inode->i_zone[i]) {
			free_block(inode->i_dev,inode->i_zone[i]);	//��s_zmap������豸��
			inode->i_zone[i]=0;
		}
	free_ind(inode->i_dev,inode->i_zone[7]);	//�ͷ�һ�����ӳ��
	free_dind(inode->i_dev,inode->i_zone[8]);	//�ͷŶ������ӳ��
	inode->i_zone[7] = inode->i_zone[8] = 0;
	inode->i_size = 0;
	inode->i_dirt = 1;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
}

