#include "x86.h"

u32 butter_psram_size();

/**
 * SYSTEM - GET EXTENDED MEMORY SIZE (286+)
 * AH = 88h
 * 
 * Returns:
 * CF clear if successful
 * AX = number of contiguous KB starting at absolute address 100000h
 * CF set on error
 * AH = status
 * 80h invalid command (PC,PCjr)
 * 86h unsupported function (XT,PS30)
 * Notes: TSRs which wish to allocate extended memory to themselves often hook this call, and return a reduced memory size. They are then free to use the memory between the new and old sizes at will.. The standard BIOS only returns memory between 1MB and 16MB; use AH=C7h for memory beyond 16MB. Not all BIOSes correctly return the carry flag, making this call unreliable unless one first checks whether it is supported through a mechanism other than calling the function and testing CF. Due to applications not dealing with more than 24-bit descriptors (286), Windows 3.0 has problems when this function reports more than 15 MB. Some releases of HIMEM.SYS are therefore limited to use only 15 MB, even when this function reports more.
 */
inline static u32 x86_int15_88() {
    return (butter_psram_size() >> 10) - 1024;
}

u32 x86_int15_hanler_C(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    switch (AH) { // AH - function
        case 0x88:
            return x86_int15_88();
    }
    return 0x8600 | CF_ON;
}
