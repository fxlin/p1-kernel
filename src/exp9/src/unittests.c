#include "plat.h"
#include "utils.h"
#include "debug.h"

static void handler(TKernelTimerHandle hTimer, void *param, void *context) {
	unsigned sec, msec; 
	current_time(&sec, &msec);
	I("%u.%03u: fired. htimer %ld, param %lx, contex %lx", sec, msec,
		hTimer, (unsigned long)param, (unsigned long)context); 
}

// to be called in a kernel process
void test_ktimer() {
	unsigned sec, msec; 

	current_time(&sec, &msec); 
	I("%u.%03u start delaying 500ms...", sec, msec); 
	ms_delay(500); 
	current_time(&sec, &msec);
	I("%u.%03u ended delaying 500ms", sec, msec); 

	// start, fire 
	int t = ktimer_start(500, handler, (void *)0xdeadbeef, (void*)0xdeaddeed);
	I("timer start. timer id %u", t); 
	ms_delay(1000);
	I("timer %d should have fired", t); 

	// start two, fire
	t = ktimer_start(500, handler, (void *)0xdeadbeef, (void*)0xdeaddeed);
	I("timer start. timer id %u", t); 
	t = ktimer_start(1000, handler, (void *)0xdeadbeef, (void*)0xdeaddeed);
	I("timer start. timer id %u", t); 
	ms_delay(2000); 
	I("both timers should have fired"); 

	// start, cancel 
	t = ktimer_start(500, handler, (void *)0xdeadbeef, (void*)0xdeaddeed);
	I("timer start. timer id %u", t);
	ms_delay(100); 
	int c = ktimer_cancel(t); 
	I("timer cancel return val = %d", c);
	BUG_ON(c < 0);

	I("there shouldn't be more callback"); 
}

void test_malloc() {
#define MIN_SIZE 32  
#define ALLOC_COUNT 4096 
    // try different sizes, small to large
    unsigned size, maxsize; 
    void *p; 
    int i; 

    // find a max allowable size
    for (size = MIN_SIZE; ; size *= 2) {
        p = malloc(size); 
        if (!p)
            break; 
        free(p); 
    }
    maxsize = size/2; 
    I("max alloc size %u", maxsize); 
    
    char ** allocs = malloc(ALLOC_COUNT * sizeof(char *));
    BUG_ON(!allocs); 

    I("BEFORE"); 
    dump_mem_info();    
    I("------------------------------");

    for (int k = 0; k < 2; k++) {
        I("---- pass %d ----- ", k); 
        for (size = MIN_SIZE; size < maxsize; size *= 2) {
            // for each size, keep allocating until fail
            for (i = 0; i < ALLOC_COUNT; i++) {
                allocs[i] = malloc(size); 
                if (!allocs[i])
                    break; 
            }
            I("size %u, count %d", size, i);
            i --; 
            // free all 
            for (; i>=0; i--) {
                free(allocs[i]); 
            }        
        }
    }
    I("should show the same as before"); 
    dump_mem_info();    
    I("------------------------------");

    // mix size alloc 
    for (int k = 0; k < 2; k++) {
        I("---- pass %d ----- ", k);     
        unsigned count = 0; 
        for (i = 0; i < ALLOC_COUNT; i++) {
            size = (i % (maxsize/2)); 
            allocs[i] = malloc(size); 
            if (!allocs[i])
                break; 
            count += size; 
        }
        I("total alloc %u bytes %d allocs", count, i); 
        i --; 
        // free all 
        for (; i>=0; i--) {
            free(allocs[i]); 
        }
    }
    I("should show the same as before"); 
    dump_mem_info();    
    I("------------------------------");

    free(allocs);
}

void test_mbox() {
    unsigned char buf[MAC_SIZE] = {0}; 
    int ret = get_mac_addr(buf);
    printf("return %d. mac is: ", ret);
    for (int i = 0; i < MAC_SIZE; i++)
        printf("%02x ", buf[i]);
    printf("\n"); 

    unsigned long s = 0; 
    get_board_serial(&s); 
    printf("serial 0x%lx\n", s); 
}

#include "uspios.h"
#include "uspi.h"

__attribute__ ((unused)) static void KeyPressedHandler(const char *pString) {
    I("received string: %s", pString);
}

__attribute__ ((unused)) static void KeyStatusHandlerRaw(unsigned char ucModifiers,
				       const unsigned char  RawKeys[6]) {
    I("received raw ev: modifier %u rawkeys %02x %02x %02x %02x %02x %02x", 
            ucModifiers, RawKeys[0], RawKeys[1], RawKeys[2], RawKeys[3], RawKeys[4], RawKeys[5]);
}

void test_usb_kb() {
	if (!USPiInitialize ()) {
		E("cannot init"); 
        return; 
	}
	if (!USPiKeyboardAvailable ())
	{
        E("kb not found");
        return; 
	}
    I("kb found... Just type something!");
    USPiKeyboardRegisterKeyPressedHandler (KeyPressedHandler); 
    // USPiKeyboardRegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw); 

	// inf loop
    for (unsigned nCount = 0; 1; nCount++) {
        USPiKeyboardUpdateLEDs(); // xzl: keep updating kb LED
    }
}

///////////////////////////// mass storage test ////////////////////////
// cf: usb/sample/storage/main.c
// for MBR, cf: https://wiki.osdev.org/MBR_(x86)
//      illustration: https://cpl.li/posts/2019-03-12-mbrfat/
//          ("MBR, LBA, FAT32" by Alexandru-Paul Copil)
#define PACKED		__attribute__ ((packed))
typedef struct TCHSAddress {
    unsigned char Head;
    unsigned char Sector : 6,
        CylinderHigh : 2;
    unsigned char CylinderLow;
} PACKED TCHSAddress;    // CHS: cylinder-head-sector addressing. mandatory

typedef struct TPartitionEntry {
    unsigned char Status;
    TCHSAddress FirstSector;
    unsigned char Type;
    TCHSAddress LastSector;
    unsigned LBAFirstSector;  // LBA: Logical Block Allocation (linear, simple addressing)
    unsigned NumberOfSectors;
} PACKED TPartitionEntry;

typedef struct TMasterBootRecord {
    unsigned char BootCode[0x1BE];
    TPartitionEntry Partition[4];
    unsigned short BootSignature;
#define BOOT_SIGNATURE 0xAA55
} PACKED TMasterBootRecord;

void test_usb_storage() {
    if (!USPiInitialize()) {
        E("cannot init");
        return;
    }

    int nDevices = USPiMassStorageDeviceAvailable();
    if (nDevices < 1) {
        E("no mass torage dev");
        return;
    }

    for (unsigned nDeviceIndex = 0; nDeviceIndex < (unsigned)nDevices; nDeviceIndex++) {
        TMasterBootRecord MBR;
        if (USPiMassStorageDeviceRead(0, &MBR, sizeof MBR, nDeviceIndex) != sizeof MBR) {
            W("read error");
            continue;
        }

        if (MBR.BootSignature != BOOT_SIGNATURE) {
            W("Boot signature not found");
            continue;
        }

        I("Dumping the partition table");
        I("# Status Type  1stSector    Sectors");

        // NB: partiion 1 could start from sector32. 
        // cf: https://unix.stackexchange.com/questions/81556/area-on-disk-after-the-mbr-and-before-the-partition-start-point
        // "The old 32KiB gap between MBR and first sector of file system is 
        //  called DOS compatibility region or MBR gap, because DOS required 
        //  that the partitions started at cylinder boundaries".
        for (unsigned nPartition = 0; nPartition < 4; nPartition++) {
            W("%u %02X     %02X   %10u %10u",
              nPartition + 1,
              (unsigned)MBR.Partition[nPartition].Status,
              (unsigned)MBR.Partition[nPartition].Type,
              MBR.Partition[nPartition].LBAFirstSector,
              MBR.Partition[nPartition].NumberOfSectors);
        }
    }
}

///////////////////
extern void fb_showpicture(void); 

void test_fb() {
    fb_showpicture();
}