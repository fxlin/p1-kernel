#include "printf.h"
#include "utils.h"
#include "timer.h"
#include "irq.h"
#include "fork.h"
#include "sched.h"
#include "mini_uart.h"

#ifdef USE_LFB
#include "lfb.h"
#endif

#ifdef USE_QEMU
#define CHAR_DELAY (5 * 5000000)
#else
#define CHAR_DELAY (1000000)
#endif

extern unsigned long *spin_cpu1; 
extern unsigned long core_flags; 

// old
#define start_core(x, f) 					\
	do {									\
		*spin_cpu##x = (unsigned long)f; 	\
		asm volatile ("sev"); 				\
	} while(0)

extern unsigned long start0;

static void kick_core(int coreid) {
	*(unsigned long *)(0xd8UL + coreid*8) = (unsigned long)&start0; 
	*(&core_flags + coreid) = 1; 
	asm volatile ("sev");
}

void secondary_core(int core_id)
{
	printf("Hello from core %d\n", core_id);
	if (core_id<3)
		kick_core(core_id+1);
	while (1)
		;
}

void process(char *array)
{
#ifdef USE_LFB // (optional) determine the init locations on the graphical console
	int scr_x, scr_y; 
	char c; 
	if (array[0] == '1') {
		scr_x = 0; scr_y = 320; 
	} else {
		scr_x = 0; scr_y = 480; 
	}
#endif 

	while (1){
		for (int i = 0; i < 5; i++){
			uart_send(array[i]);
#ifdef USE_LFB  // (optional) output to the graphical console
			c = array[i+1]; array[i+1]='\0';
			lfb_print_update(&scr_x, &scr_y, array+i);
			array[i+1] = c; 
			if (scr_x > 1024)
				lfb_print_update(&scr_x, &scr_y, "\n");
#endif
			delay(CHAR_DELAY);
		} 
		schedule(); // yield
	}
}

void kernel_main(int core_id)
{
	if (core_id==0) {
		uart_init();
		init_printf(0, putc);
	}

	printf("kernel boots core_id %d\r\n", core_id);	

#ifdef USE_LFB // (optional) init output to the graphical console
	lfb_init(); 
	lfb_showpicture();
	lfb_print(0, 240, "kernel boots");
#endif		

	kick_core(1);
	// kick_core(2);
	// kick_core(3);

	int res = copy_process((unsigned long)&process, (unsigned long)"12345");
	if (res != 0) {
		printf("error while starting process 1");
		return;
	}
	
	res = copy_process((unsigned long)&process, (unsigned long)"abcde");
	if (res != 0) {
		printf("error while starting process 2");
		return;
	}

	while (1) {
		schedule();
	}	
}
