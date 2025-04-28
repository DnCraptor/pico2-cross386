#include "x86.h"

/**
 * BIOS - GET EQUIPMENT LIST
 * Returns:
 * (E)AX = BIOS equipment list word (see #00226,#03215 at INT 4B"Tandy")
 * 
 * Note: Since older BIOSes do not know of the existence of EAX, the high word of EAX should be cleared before this call if any of the high bits will be tested
 * 
 * See Also: INT 4B"Tandy 2000" - MEM 0040h:0010h
 */
u32 x86_int11_hanler_C() {
    u16* BDA = (u16*)X86_FAR_PTR(0x0040, 0x0010);
    return *BDA;
}
