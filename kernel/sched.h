#ifndef _SCHED_H
#define _SCHED_H

#define THREAD_CPU_CONTEXT			0 		// offset of cpu_context in task_struct 

#ifndef __ASSEMBLER__

#define THREAD_SIZE				4096

#define FIRST_TASK task[0]
#define LAST_TASK task[NR_TASKS-1]

/* 
	By classic OS textbook/lectures, 
	READY means ready to run but is not running because of no idle CPU, not scheduled yet, etc. 
	RUNNING means the task is running and currently uses CPU.

	However, in the simple impl. below, 
	TASK_RUNNING represents either RUNNING or READY 
*/
#define TASK_UNUSED						0  // unused tcb slot
#define TASK_RUNNING					1
#define TASK_SLEEPING					2
#define TASK_ZOMBIE						3
#define TASK_RUNNABLE					4

/* TODO: define more task states (as constants) below, e.g. TASK_WAIT */

#define PF_KTHREAD		            	0x00000002	

extern struct task_struct *current; // sched.c
extern struct task_struct * task[NR_TASKS];
extern int nr_tasks;

// Contains values of all registers that might be different between the tasks.
// x0-x7 func call arguments; x9-x15 caller saved; x19-x29 callee saved
struct cpu_context {	
	unsigned long x19;
	unsigned long x20;
	unsigned long x21;
	unsigned long x22;
	unsigned long x23;
	unsigned long x24;
	unsigned long x25;
	unsigned long x26;
	unsigned long x27;
	unsigned long x28;
	unsigned long fp;
	unsigned long sp;
	unsigned long pc;
};
// 13 regs

struct user_page {
	unsigned long phys_addr;
	unsigned long virt_addr; // user va
};

// the size grows with MAX_TASK_XXX_PAGES, could be problem for larger user programs in the future...
struct mm_struct {
	unsigned long pgd;	// pa. this is loaded to ttbr0 (user va)
	
	unsigned long sz, codesz; 	// for a user task, VA [0, sz) covers its code,data,&heap. [0,codesz) covers code &data. not page aligned
	int user_pages_count;
	/* keep track of which user pages are used for this task */
	// struct user_page user_pages[MAX_TASK_USER_PAGES];

	int kernel_pages_count;
	/* which kernel pages are used by this task, e.g. those for pgtables.  PA */
	unsigned long kernel_pages[MAX_TASK_KER_PAGES]; 
};

#include "spinlock.h"

// the metadata describing a task
struct task_struct {
	struct cpu_context cpu_context;	// register values. must come first. 
	int killed;                  // If non-zero, have been killed. checked by entry.S. 

	long state;		// the state of the current task, e.g. TASK_RUNNING
	long counter;	// how long this task has been running? decreases by 1 each timer tick. Reaching 0, kernel will attempt to schedule another task. Support our simple sched
	long priority;	// when kernel schedules a new task, the kernel copies the task's  `priority` value to `counter`. Regulate CPU time the task gets relative to other tasks 
	long preempt_count; // a flag. A non-zero means that the task is executing in a critical code region cannot be interrupted, Any timer tick should be ignored and not triggering rescheduling
	unsigned long flags;
	struct spinlock lock;	 // protect this task_struct
	struct mm_struct mm;		// XXX to impl thread, this shall be a pointer 
	void *chan;                  // If non-zero, sleeping on chan
	int pid; 					 // still need this, ease of debugging...
  	int xstate;  				// Exit status to be returned to parent's wait

	// wait_lock must be held when using this:
	struct task_struct *parent;         // Parent process

	struct file *ofile[NOFILE];  // Open files
	struct inode *cwd;           // Current directory
  	char name[16];               // Process name (debugging)	
};

// use this to check struct size at compile time
// https://stackoverflow.com/questions/11770451/what-is-the-meaning-of-attribute-packed-aligned4
// char (*__kaboom)[sizeof(struct task_struct)] = 1; 

// bottom half a page; make sure the top half enough space for ker stack...
_Static_assert(sizeof(struct task_struct) < 1200);	// 1408 seems too big, corrupt stack?

// --------------- processor related ----------------------- // 
// we only support 1 cpu (as of now), but xv6 code is around multicore so we keep the 
// ds here...
struct cpu {
  struct task_struct *proc;          // The process running on this cpu, or null.
  int noff;                   		// Depth of push_off() nesting.
  int intena;                 		// Were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU];		// sched.c
static inline struct cpu* mycpu(void) {return &cpus[0];};
static inline struct task_struct *myproc(void) {return current;};

// --------------- fork related ----------------------- // 

/*
 * PSR bits
 */
#define PSR_MODE_EL0t	0x00000000
#define PSR_MODE_EL1t	0x00000004
#define PSR_MODE_EL1h	0x00000005
#define PSR_MODE_EL2t	0x00000008
#define PSR_MODE_EL2h	0x00000009
#define PSR_MODE_EL3t	0x0000000c
#define PSR_MODE_EL3h	0x0000000d


struct pt_regs {
	unsigned long regs[31];
	unsigned long sp;
	unsigned long pc;
	unsigned long pstate;
};

#define MEMBER_OFFSET(type, member) ((long)(&((type *)0)->member))
#define TASK_STRUCT_KILLED_OFFSET_C MEMBER_OFFSET(struct task_struct, killed)
#endif		// ! __ASSEMBLER__

// exposed to asm...
#define TASK_STRUCT_KILLED_OFFSET	(13*8) 	// cpu_context size, 13 regs
#define S_FRAME_SIZE			272 		// size of all saved registers 
#define S_X0				    0		// offset of x0 register in saved stack frame

#ifndef __ASSEMBLER__
_Static_assert(TASK_STRUCT_KILLED_OFFSET == TASK_STRUCT_KILLED_OFFSET_C);
#endif

#endif
