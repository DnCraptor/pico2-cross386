#include "x86.h"

u16 X86_CS = 0xF000;
u16 X86_DS = 0;
u16 X86_ES = 0;
u16 X86_FS = 0;
u16 X86_GS = 0;
u16 X86_SS = 0;
u32 X86_CR0 = 0x00000010; // PE=0, ET=1
