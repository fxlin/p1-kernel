#ifndef	_USER_SYS_H
#define	_USER_SYS_H

int call_sys_write(int fd, char * buf, int n);
int call_sys_open(const char * buf, int omode);
int call_sys_mknod(const char * buf, short major, short minor);
int call_sys_dup(int fd);

int call_sys_fork();
int call_sys_exit(int);

// xzl: TODO move non syscalls out... as inline asm . 
extern unsigned long get_sp ( void );
extern unsigned long get_pc ( void );

#endif  /*_USER_SYS_H */