#define __LIBRARY__
#include <unistd.h>

volatile void _exit(int exit_code)
{
//����Ƕ������˼�ǣ�
//	movl _NR_exit %eax
//	movl exit_code %ebx
//	int $0x80	//$0x80��LINUX���жϵ������
	__asm__("int $0x80"::"a" (__NR_exit),"b" (exit_code));
}
