#include "utils.h"
#include "entry.h"

#ifdef PLAT_VIRT
#include "gic.h"
#endif

// must match entry.h 
const char *entry_error_messages[] = {
    [SYNC_INVALID_EL1t] "SYNC_INVALID_EL1t",
    [IRQ_INVALID_EL1t] "IRQ_INVALID_EL1t",		
    [FIQ_INVALID_EL1t] "FIQ_INVALID_EL1t",		
    [ERROR_INVALID_EL1t] "ERROR_INVALID_EL1t",		

    [SYNC_INVALID_EL1h] "SYNC_INVALID_EL1h",		
    // [IRQ_INVALID_EL1h] "IRQ_INVALID_EL1h",		
    [FIQ_INVALID_EL1h] "FIQ_INVALID_EL1h",		
    [ERROR_INVALID_EL1h] "ERROR_INVALID_EL1h",		

    // [SYNC_INVALID_EL0_64] "SYNC_INVALID_EL0_64",		
    // [IRQ_INVALID_EL0_64]"IRQ_INVALID_EL0_64",		
    [FIQ_INVALID_EL0_64] "FIQ_INVALID_EL0_64",		
    [ERROR_INVALID_EL0_64] "ERROR_INVALID_EL0_64",	

    [SYNC_INVALID_EL0_32] "SYNC_INVALID_EL0_32",		
    [IRQ_INVALID_EL0_32] "IRQ_INVALID_EL0_32",		
    [FIQ_INVALID_EL0_32] "FIQ_INVALID_EL0_32",		
    [ERROR_INVALID_EL0_32] "ERROR_INVALID_EL0_32",

    [SYNC_ERROR]    "Unhandled EL0 sync exception",
    [SYSCALL_ERROR] "SYSCALL_ERROR",
    [DATA_ABORT_ERROR] "Unhandled EL0 data abort (after kernel's trying)"
};

// Enables Core 0 Timers interrupt control for the generic timer 
void enable_interrupt_controller()
{
#if 0
    // Explanation: We have to deal with yet another Rpi3 quirk. The Arm generic timer IRQs are wired to a per-core interrupt controller/register. 
    // For core 0, this is `TIMER_INT_CTRL_0` at 0x40000040; bit 1 is for physical timer at EL1 (CNTP). This register is documented 
    // in the [manual](https://www.raspberrypi.org/documentation/hardware/raspberrypi/bcm2836/QA7_rev3.4.pdf) of BCM2836 
    // (search for "Core timers interrupts"). Note the manual is NOT for the BCM2837 SoC used by Rpi3    
    put32(TIMER_INT_CTRL_0, TIMER_INT_CTRL_0_VALUE);
#endif

#ifdef PLAT_VIRT
    arm_gic_dist_init(0 /* core */, VA_START + QEMU_GIC_DIST_BASE, 0 /*irq start*/);
    arm_gic_cpu_init(0 /* core*/, VA_START + QEMU_GIC_CPU_BASE);
    arm_gic_umask(0 /* core */, IRQ_ARM_GENERIC_TIMER);
    arm_gic_umask(0 /* core */, IRQ_UART_PL011);
    arm_gic_umask(0 /* core */, VIRTIO0_IRQ);

    // finding irq numbers, which I couldn't find figure out (qemu info qtree? trace events)?
    // for (int i=0; i<64; i++)
    //     arm_gic_umask(0, i);

    // gic_dump(); // debugging 
#endif
}

// esr: syndrome, elr: ~faulty pc, far: faulty access addr
void show_invalid_entry_message(int type, unsigned long esr, 
    unsigned long elr, unsigned long far)
{    
    E("%s, esr: 0x%016lx, elr: 0x%016lx, far: 0x%016lx",  
        entry_error_messages[type], esr, elr, far);
    E("online esr decoder: %s0x%016lx", "https://esr.arm64.dev/#", esr);
    // TODO: dump FAR
}

// called from hw irq handler (el1_irq, entry.S)
void handle_irq(void)
{
    uint64_t cpu = 0; 
    
    int irq = arm_gic_get_active_irq(cpu);
    arm_gic_ack(cpu, irq);

    switch (irq) {
        case (IRQ_ARM_GENERIC_TIMER):
            handle_generic_timer_irq();
            break;
        case IRQ_UART_PL011:
            uartintr();
            break; 
        case VIRTIO0_IRQ:
            virtio_disk_intr();
            break; 
        default:
            printf("Unknown irq: %d\r\n", irq);
    }

#if 0    
    // Interrupt controller can help us with this job: it has `INT_SOURCE_0` register that holds interrupt status for interrupts `0 - 31`. 
    // Using this register we can check whether the current interrupt was generated by the timer or by some other device and call device specific interrupt handler
    // NB: Each Core has its own pending local intrrupts register. 
    unsigned int irq = get32(INT_SOURCE_0);
    switch (irq) {
        case (GENERIC_TIMER_INTERRUPT):
            handle_generic_timer_irq();
            break;
        default:
            printf("Unknown pending irq: %x\r\n", irq);
    }
#endif    
}