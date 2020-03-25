/*
 * When in kernel-mode, we cannot use printf, as fs is liable to
 * point to 'interesting' things. Make a printf with fs-saving, and
 * all is well.
 */
#include <stdarg.h>
#include <stddef.h>

#include <linux/kernel.h>

static char buf[1024];

int printk(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);			//��λҪ��ӡ�Ĵ���args�еĿ�ʼλ��
	i=vsprintf(buf,fmt,args);		//Ҫ��ӡ���ַ�����
	va_end(args);
	__asm__("push %%fs\n\t"
		"push %%ds\n\t"
		"pop %%fs\n\t"
		"pushl %0\n\t"		//Ҫ��ӡ���ַ�������i��tty_write�����ĵ�3������
		"pushl $_buf\n\t"		//Ҫ��ӡ���ַ���ָ�룬tty_write�����ĵ�2������
		"pushl $0\n\t"		//tty_write�����ĵ�һ��������Ϊ0
		"call _tty_write\n\t"
		"addl $8,%%esp\n\t"
		"popl %0\n\t"
		"pop %%fs"
		::"r" (i):"ax","cx","dx");
	return i;
}
