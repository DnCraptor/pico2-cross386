#ifndef X86_8259A_H
#define X86_8259A_H

#ifdef __cplusplus
extern "C" {
#endif

#include "x86.h"
// 8259A PIC (Master + Slave)
void x86_8259A_Master_PIC(u8);
void x86_8259A_Slave_PIC(u8);
void x86_8259A_Master_PIC_D(u8);
void x86_8259A_Slave_PIC_D(u8);

void X86_IRQ1(void);
void X86_IRQ2(void);
void X86_IRQ3(void);
void X86_IRQ4(void);
void X86_IRQ5(void);
void X86_IRQ6(void);
void X86_IRQ7(void);

#ifdef __cplusplus
}
#endif

#endif
