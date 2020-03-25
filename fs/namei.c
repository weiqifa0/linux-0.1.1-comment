#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <const.h>
#include <sys/stat.h>

#define ACC_MODE(x) ("\004\002\006\377"[(x)&O_ACCMODE])

/*
 * comment out this line if you want names > NAME_LEN chars to be
 * truncated. Else they will be disallowed.
 */
/* #define NO_TRUNCATE */

#define MAY_EXEC 1
#define MAY_WRITE 2
#define MAY_READ 4

/*
 *	permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * I don't know if we should look at just the euid or both euid and
 * uid, but that should be easily changed.
 */
static int permission(struct m_inode * inode,int mask)		//Ȩ�޼��
{
	int mode = inode->i_mode;

/* special case: not even root can read/write a deleted file */
	if (inode->i_dev && !inode->i_nlinks)
		return 0;
	if (!(current->uid && current->euid))			//uid��euid��Ϊ0����ʾ���Ȩ��
		mode=0777;
	else if (current->uid==inode->i_uid || current->euid==inode->i_uid)		//�û�Ȩ�޼��
		mode >>= 6;
	else if (current->gid==inode->i_gid || current->egid==inode->i_gid)		//�û���Ȩ�޼��
		mode >>= 3;
	return mode & mask & 0007;
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, match returns 1 for success, 0 for failure.
 */
static int match(int len,const char * name,struct dir_entry * de)
{
	register int same __asm__("ax");

	if (!de || !de->inode || len > NAME_LEN)		//���Ŀ¼ָ��Ϊ�ջ�ڵ㲻���ڻ򳤶�̫��
		return 0;
	if (len < NAME_LEN && de->name[len])
		return 0;
//������Ƕ������˼�ǣ�
//	movl	0	%eax
//	movl	name	%esi
//	movl	de->name	%edi
//	cld
//	repe				//ZF=1
//	cmpsb
//	setz	%al
//	movl	%eax	same
	__asm__("cld\n\t"
		"fs ; repe ; cmpsb\n\t"
		"setz %%al"
		:"=a" (same)
		:"0" (0),"S" ((long) name),"D" ((long) de->name),"c" (len)
		:"cx","di","si");
	return same;
}

/*
 *	find_entry()
 *
 * finds and entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 */
static struct buffer_head * find_entry(struct m_inode * dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int entries;
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;

#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
	entries = dir->i_size / (sizeof (struct dir_entry));	//��Ŀ¼������������
	*res_dir = NULL;
	if (!namelen)
		return NULL;
	if (!(block = dir->i_zone[0]))				//��Ŀ¼����û��Ԫ��
		return NULL;
	if (!(bh = bread(dir->i_dev,block)))			//��Ӳ�̶�ȡ��Ŀ¼
		return NULL;
	i = 0;
	de = (struct dir_entry *) bh->b_data;			//deָ��Ŀ¼�ļ��������Ŀ�ʼ
	while (i < entries) {
		if ((char *)de >= BLOCK_SIZE+bh->b_data) {	//�����Ŀ¼�ļ���һ���������δ�ҵ����Ͷ���Ŀ¼�ļ�����һ����
			brelse(bh);					//�ͷ�bh
			bh = NULL;
			if (!(block = bmap(dir,i/DIR_ENTRIES_PER_BLOCK)) ||
			    !(bh = bread(dir->i_dev,block))) {	//Ѱ����һ�鲢���������ڴ�
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;
		}
		if (match(namelen,name,de)) {			//�Ա�Ŀ¼�ļ��е�һ��Ԫ�أ�����1���ҵ�
			*res_dir = de;				//ָ���������ݵ�ƫ����
			return bh;
		}
		de++;
		i++;
	}
	brelse(bh);
	return NULL;
}

/*
 *	add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
static struct buffer_head * add_entry(struct m_inode * dir,
	const char * name, int namelen, struct dir_entry ** res_dir)
{
	int block,i;
	struct buffer_head * bh;
	struct dir_entry * de;

	*res_dir = NULL;
#ifdef NO_TRUNCATE
	if (namelen > NAME_LEN)
		return NULL;
#else
	if (namelen > NAME_LEN)
		namelen = NAME_LEN;
#endif
	if (!namelen)						//�������Ŀ¼��û���ļ��������ؿ�
		return NULL;
	if (!(block = dir->i_zone[0]))			//ȡ��Ŀ¼�ļ��ĵ�һ�����
		return NULL;
	if (!(bh = bread(dir->i_dev,block)))		//��ȡĿ¼�ļ��ĵ�һ��
		return NULL;
	i = 0;
	de = (struct dir_entry *) bh->b_data;		//deָ���һ���������
	while (1) {
		if ((char *)de >= BLOCK_SIZE+bh->b_data) {	//���deָ���˸ÿ��ĩβ
			brelse(bh);
			bh = NULL;
			block = create_block(dir,i/DIR_ENTRIES_PER_BLOCK);	//ȡ��Ŀ¼�ļ�����һ����ţ������Ҫ���ʹ����¿�
			if (!block)
				return NULL;
			if (!(bh = bread(dir->i_dev,block))) {	//���˿����buffer
				i += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			de = (struct dir_entry *) bh->b_data;	//deָ��ÿ��������
		}
		if (i*sizeof(struct dir_entry) >= dir->i_size) {	//��Ŀ¼�ļ���ĩβ�����µĽڵ㣬��ʼ��Ϊ0�����޸ĸ�Ŀ¼�ļ��ĳߴ�
			de->inode=0;
			dir->i_size = (i+1)*sizeof(struct dir_entry);
			dir->i_dirt = 1;
			dir->i_ctime = CURRENT_TIME;
		}
		if (!de->inode) {
			dir->i_mtime = CURRENT_TIME;
			for (i=0; i < NAME_LEN ; i++)
				de->name[i]=(i<namelen)?get_fs_byte(name+i):0;	//���ļ���д��de->name
			bh->b_dirt = 1;
			*res_dir = de;			//��ʱde��һ��inode=0��name=�ļ����Ľṹ
			return bh;
		}
		de++;
		i++;
	}
	brelse(bh);
	return NULL;
}

/*
 *	get_dir()
 *
 * Getdir traverses the pathname until it hits the topmost directory.
 * It returns NULL on failure.
 */
static struct m_inode * get_dir(const char * pathname)
{
	char c;
	const char * thisname;
	struct m_inode * inode;
	struct buffer_head * bh;
	int namelen,inr,idev;
	struct dir_entry * de;

	if (!current->root || !current->root->i_count)
		panic("No root inode");
	if (!current->pwd || !current->pwd->i_count)
		panic("No cwd inode");
	if ((c=get_fs_byte(pathname))=='/') {		//ȡ��һ���ֽ�
		inode = current->root;
		pathname++;
	} else if (c)
		inode = current->pwd;
	else
		return NULL;	/* empty name is bad */
	inode->i_count++;
	while (1) {
		thisname = pathname;
		if (!S_ISDIR(inode->i_mode) || !permission(inode,MAY_EXEC)) {		//�������Ŀ¼��û��Ȩ��
			iput(inode);				//�ͷŸýڵ�
			return NULL;
		}
		for(namelen=0;(c=get_fs_byte(pathname++))&&(c!='/');namelen++)	//ȡ��Ŀ¼����
			/* nothing */ ;
		if (!c)
			return inode;				//ȡ��ȫ��Ŀ¼���أ�ѭ������
		if (!(bh = find_entry(inode,thisname,namelen,&de))) {
			iput(inode);
			return NULL;
		}
		inr = de->inode;				//ȡ��Ŀ¼��inode
		idev = inode->i_dev;
		brelse(bh);
		iput(inode);
		if (!(inode = iget(idev,inr)))		//ȡ�ø�Ŀ¼���ڴ�inode��ΪѰ����һ����Ŀ¼��׼��
			return NULL;
	}
}

/*
 *	dir_namei()
 *
 * dir_namei() returns the inode of the directory of the
 * specified name, and the name within that directory.
 */
static struct m_inode * dir_namei(const char * pathname,
	int * namelen, const char ** name)
{
	char c;
	const char * basename;
	struct m_inode * dir;

	if (!(dir = get_dir(pathname)))			//ȡ�ø�Ŀ¼�������Ŀ¼���ڴ�inode
		return NULL;
	basename = pathname;					//����ָ��ָ��ͬһ��ַ�����ļ�ȫ������ʼ��ַ
	while (c=get_fs_byte(pathname++))
		if (c=='/')
			basename=pathname;			//basename���ս���Ҫ���еĳ�����
	*namelen = pathname-basename-1;
	*name = basename;
	return dir;
}

/*
 *	namei()
 *
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 */
struct m_inode * namei(const char * pathname)
{
	const char * basename;
	int inr,dev,namelen;
	struct m_inode * dir;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (!(dir = dir_namei(pathname,&namelen,&basename)))		//ȡ��Ŀ¼���ڴ�inode
		return NULL;
	if (!namelen)			/* special case: '/usr/' etc */
		return dir;
	bh = find_entry(dir,basename,namelen,&de);			//ȡ����Ҫ���л�򿪵�inode
	if (!bh) {					//���û������ļ����ͷŸ�Ŀ¼��buffer
		iput(dir);
		return NULL;
	}
	inr = de->inode;				//ȡ�ø��ļ���Ӳ��inode
	dev = dir->i_dev;
	brelse(bh);
	iput(dir);
	dir=iget(dev,inr);				//ȡ�ø��ļ����ڴ�inode
	if (dir) {
		dir->i_atime=CURRENT_TIME;
		dir->i_dirt=1;
	}
	return dir;
}

/*
 *	open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.
 */
int open_namei(const char * pathname, int flag, int mode,
	struct m_inode ** res_inode)
{
	const char * basename;
	int inr,dev,namelen;
	struct m_inode * dir, *inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if ((flag & O_TRUNC) && !(flag & O_ACCMODE))		//���flag�ǿ��޼��Ļ�ɷ��ʵ�
		flag |= O_WRONLY;					//��ô���Ͽ�дλ
	mode &= 0777 & ~current->umask;				//mode������ܵ�ǰ���������Լ��
	mode |= I_REGULAR;
	if (!(dir = dir_namei(pathname,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {			/* special case: '/usr/' etc */	//���pathnameֻ��Ŀ¼����û���ļ���
		if (!(flag & (O_ACCMODE|O_CREAT|O_TRUNC))) {
			*res_inode=dir;
			return 0;
		}
		iput(dir);
		return -EISDIR;
	}
	bh = find_entry(dir,basename,namelen,&de);
	if (!bh) {					//�����Ŀ¼��û�з��ָ��ļ�����ڵ�
		if (!(flag & O_CREAT)) {		//���flag��û�н���λ������
			iput(dir);
			return -ENOENT;
		}
		if (!permission(dir,MAY_WRITE)) {		//�����Ŀ¼��Ȩ���ǲ���д�ģ�����
			iput(dir);
			return -EACCES;
		}
		inode = new_inode(dir->i_dev);	//Ϊ�����Ŀ¼�в����ڵ��ļ�����һ���µ�inode
		if (!inode) {
			iput(dir);
			return -ENOSPC;
		}
		inode->i_mode = mode;		//�����µ�inode��mode
		inode->i_dirt = 1;
		bh = add_entry(dir,basename,namelen,&de);		//��Ŀ¼�ļ���д��de->name
		if (!bh) {
			inode->i_nlinks--;
			iput(inode);
			iput(dir);
			return -ENOSPC;
		}
		de->inode = inode->i_num;		//��de->inodeд��ʵ�ʿ�ţ�������dir����de->name����
		bh->b_dirt = 1;
		brelse(bh);
		iput(dir);
		*res_inode = inode;
		return 0;
	}
	inr = de->inode;
	dev = dir->i_dev;
	brelse(bh);
	iput(dir);
	if (flag & O_EXCL)				//���flag�Ǽ����ļ���û��
		return -EEXIST;
	if (!(inode=iget(dev,inr)))			//ȡ���ļ���inode
		return -EACCES;
	if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) ||
	    permission(inode,ACC_MODE(flag))!=ACC_MODE(flag)) {
		iput(inode);
		return -EPERM;
	}
	inode->i_atime = CURRENT_TIME;
	if (flag & O_TRUNC)
		truncate(inode);
	*res_inode = inode;
	return 0;
}

int sys_mkdir(const char * pathname, int mode)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh, *dir_block;
	struct dir_entry * de;

	if (current->euid && current->uid)
		return -EPERM;
	if (!(dir = dir_namei(pathname,&namelen,&basename)))		//��ȡ����Ŀ¼���Ǹ�Ŀ¼
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {		//����Ŀ¼��дȨ��
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(dir,basename,namelen,&de);	//�����Ŀ¼�ļ����Ƿ��д���Ŀ¼������
	if (bh) {
		brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = new_inode(dir->i_dev);		//����һ���µ�inode
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_size = 32;
	inode->i_dirt = 1;
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	if (!(inode->i_zone[0]=new_block(inode->i_dev))) {	//Ϊ��Ŀ¼�ڵ㴴����һ����
		iput(dir);
		inode->i_nlinks--;
		iput(inode);
		return -ENOSPC;
	}
	inode->i_dirt = 1;
	if (!(dir_block=bread(inode->i_dev,inode->i_zone[0]))) {	//����Ŀ¼��
		iput(dir);
		free_block(inode->i_dev,inode->i_zone[0]);
		inode->i_nlinks--;
		iput(inode);
		return -ERROR;
	}
	de = (struct dir_entry *) dir_block->b_data;		//deָ��ÿ��������
	de->inode=inode->i_num;			//������Ŀ¼�����
	strcpy(de->name,".");
	de++;
	de->inode = dir->i_num;			//������Ŀ¼�����
	strcpy(de->name,"..");
	inode->i_nlinks = 2;
	dir_block->b_dirt = 1;
	brelse(dir_block);
	inode->i_mode = I_DIRECTORY | (mode & 0777 & ~current->umask);	//�����ڵ���ΪĿ¼�ļ�
	inode->i_dirt = 1;
	bh = add_entry(dir,basename,namelen,&de);		//�ڸ�Ŀ¼�еǼǱ�Ŀ¼�ļ�
	if (!bh) {
		iput(dir);
		free_block(inode->i_dev,inode->i_zone[0]);
		inode->i_nlinks=0;
		iput(inode);
		return -ENOSPC;
	}
	de->inode = inode->i_num;
	bh->b_dirt = 1;
	dir->i_nlinks++;
	dir->i_dirt = 1;
	iput(dir);
	iput(inode);
	brelse(bh);
	return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
static int empty_dir(struct m_inode * inode)
{
	int nr,block;
	int len;
	struct buffer_head * bh;
	struct dir_entry * de;

	len = inode->i_size / sizeof (struct dir_entry);	//��Ŀ¼���м�����Ŀ
	if (len<2 || !inode->i_zone[0] ||
	    !(bh=bread(inode->i_dev,inode->i_zone[0]))) {	//���Ŀ¼�Ƿ�Ϊ��ЧĿ¼
	    	printk("warning - bad directory on dev %04x\n",inode->i_dev);
		return 0;
	}
	de = (struct dir_entry *) bh->b_data;			//deָ��Ŀ¼�е�Ԫ��
	if (de[0].inode != inode->i_num || !de[1].inode || 
	    strcmp(".",de[0].name) || strcmp("..",de[1].name)) {	//���Ԫ���еĵ�һ���Ƿ�Ϊ�������͵ڶ����Ƿ�Ϊ��������
	    	printk("warning - bad directory on dev %04x\n",inode->i_dev);
		return 0;
	}
	nr = 2;
	de += 2;
	while (nr<len) {
		if ((void *) de >= (void *) (bh->b_data+BLOCK_SIZE)) {		//�����Ŀ¼�ļ��ܴ󣬳����˱���
			brelse(bh);
			block=bmap(inode,nr/DIR_ENTRIES_PER_BLOCK);	//�ұ�Ŀ¼����һ����
			if (!block) {
				nr += DIR_ENTRIES_PER_BLOCK;
				continue;
			}
			if (!(bh=bread(inode->i_dev,block)))		//��ȡ�ÿ�
				return 0;
			de = (struct dir_entry *) bh->b_data;		//���¶�λָ��de
		}
		if (de->inode) {			//���Ŀ¼�ļ���ֻ�б�Ŀ¼�͸�Ŀ¼���ô����nullĿ¼
			brelse(bh);
			return 0;
		}
		de++;
		nr++;
	}
	brelse(bh);
	return 1;
}

int sys_rmdir(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (current->euid && current->uid)
		return -EPERM;
	if (!(dir = dir_namei(name,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	bh = find_entry(dir,basename,namelen,&de);	//��Ŀ¼�ļ����Ƿ���Ҫɾ�����Ǹ�Ŀ¼��
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {			//���дȨ��
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {	//ȡҪɾ�����Ǹ��ļ���inode
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (inode == dir) {	/* we may not delete ".", but "../dir" is ok */		//Ҫɾ����Ŀ¼�����Ǹ�Ŀ¼
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!S_ISDIR(inode->i_mode)) {			//�˽ڵ������Ŀ¼
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTDIR;
	}
	if (!empty_dir(inode)) {				//�˽ڵ��Ƿ�Ϊ��
		iput(inode);
		iput(dir);
		brelse(bh);
		return -ENOTEMPTY;
	}
	if (inode->i_nlinks != 2)
		printk("empty directory has nlink!=2 (%d)",inode->i_nlinks);
	de->inode = 0;					//�ڸ�Ŀ¼�������Ŀ¼�Ľڵ�
	bh->b_dirt = 1;
	brelse(bh);
	inode->i_nlinks=0;
	inode->i_dirt=1;
	dir->i_nlinks--;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_dirt=1;
	iput(dir);
	iput(inode);
	return 0;
}

int sys_unlink(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (!(dir = dir_namei(name,&namelen,&basename)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	inode = iget(dir->i_dev, de->inode);
	if (!inode) {					//�������᲻���ڵ��ļ�
		printk("iget failed in delete (%04x:%d)",dir->i_dev,de->inode);
		iput(dir);
		brelse(bh);
		return -ENOENT;
	}
	if (!S_ISREG(inode->i_mode)) {		//�Ƿ�Ϊ�����Ľڵ�mode
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!inode->i_nlinks) {
		printk("Deleting nonexistent file (%04x:%d), %d\n",
			inode->i_dev,inode->i_num,inode->i_nlinks);
		inode->i_nlinks=1;
	}
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	inode->i_nlinks--;
	inode->i_dirt = 1;
	inode->i_ctime = CURRENT_TIME;
	iput(inode);
	iput(dir);
	return 0;
}

int sys_link(const char * oldname, const char * newname)
{
	struct dir_entry * de;
	struct m_inode * oldinode, * dir;
	struct buffer_head * bh;
	const char * basename;
	int namelen;

	oldinode=namei(oldname);			//�������ļ�����inode
	if (!oldinode)
		return -ENOENT;
	if (!S_ISREG(oldinode->i_mode)) {		//�����������ļ�
		iput(oldinode);
		return -EPERM;
	}
	dir = dir_namei(newname,&namelen,&basename);	//�ҳ�newname�ĸ�Ŀ¼�ڵ�
	if (!dir) {
		iput(oldinode);
		return -EACCES;
	}
	if (!namelen) {
		iput(oldinode);
		iput(dir);
		return -EPERM;
	}
	if (dir->i_dev != oldinode->i_dev) {
		iput(dir);
		iput(oldinode);
		return -EXDEV;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		iput(oldinode);
		return -EACCES;
	}
	bh = find_entry(dir,basename,namelen,&de);	//������Ŀ¼���Ƿ����������
	if (bh) {						//����У��Ͳ��ܽ���
		brelse(bh);
		iput(dir);
		iput(oldinode);
		return -EEXIST;
	}
	bh = add_entry(dir,basename,namelen,&de);		//�ڸ�Ŀ¼��Ϊ������ֽ���һ����ڵ�
	if (!bh) {
		iput(dir);
		iput(oldinode);
		return -ENOSPC;
	}
	de->inode = oldinode->i_num;			//���˽ڵ�ָ���oldinodeһ����inode
	bh->b_dirt = 1;
	brelse(bh);
	iput(dir);
	oldinode->i_nlinks++;
	oldinode->i_ctime = CURRENT_TIME;
	oldinode->i_dirt = 1;
	iput(oldinode);
	return 0;
}
