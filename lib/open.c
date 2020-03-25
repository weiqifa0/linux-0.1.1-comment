#define __LIBRARY__
#include <unistd.h>
#include <stdarg.h>

int open(const char * filename, int flag, ...)
{
	register int res;
	va_list arg;

	va_start(arg,flag);
//�������Ƕ������˼�ǣ�
//	movl	_NR_open	%eax
//	movl	filename	%ebx
//	movl	flag	%ecx
//	movl	va_arg(arg,int)	%edx
//	int	0x80				//ϵͳ��������
//	movl	%eax	res
	__asm__("int $0x80"
		:"=a" (res)
		:"0" (__NR_open),"b" (filename),"c" (flag),
		"d" (va_arg(arg,int)));
	if (res>=0)
		return res;
	errno = -res;
	return -1;
}
