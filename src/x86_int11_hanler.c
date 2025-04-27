#include "x86.h"

u32 x86_int11_hanler_C() {
    u16* BDA = (u16*)X86_FAR_PTR(0x0040, 0x0010);
    return *BDA;
}
