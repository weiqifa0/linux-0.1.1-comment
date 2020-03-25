extern inline unsigned char get_fs_byte(const char * addr)
{
	unsigned register char _v;

//������Ƕ������˼��:
//	movb	%fs:*addr	_v
	__asm__ ("movb %%fs:%1,%0":"=r" (_v):"m" (*addr));
	return _v;
}

extern inline unsigned short get_fs_word(const unsigned short *addr)
{
	unsigned short _v;

//������Ƕ������˼��:
//	movw	%fs:*addr	_v
	__asm__ ("movw %%fs:%1,%0":"=r" (_v):"m" (*addr));
	return _v;
}

extern inline unsigned long get_fs_long(const unsigned long *addr)
{
	unsigned long _v;

//������Ƕ������˼��:
//	movl	fs:*addr		_v
	__asm__ ("movl %%fs:%1,%0":"=r" (_v):"m" (*addr)); \
	return _v;
}

extern inline void put_fs_byte(char val,char *addr)
{
//������Ƕ������˼��:
//	movb	val	%fs:*addr
__asm__ ("movb %0,%%fs:%1"::"r" (val),"m" (*addr));
}

extern inline void put_fs_word(short val,short * addr)
{
//������Ƕ������˼��:
//	movw	val	%fs:*addr
__asm__ ("movw %0,%%fs:%1"::"r" (val),"m" (*addr));
}

extern inline void put_fs_long(unsigned long val,unsigned long * addr)
{
//������Ƕ������˼��:
//	movl	val	%fs:*addr
__asm__ ("movl %0,%%fs:%1"::"r" (val),"m" (*addr));
}
