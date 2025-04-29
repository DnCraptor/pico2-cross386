#ifndef X86_PORTS_H
#define X86_PORTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "x86.h"

extern uint8_t X86_PORTS[64 << 10];

void x86_port_hanle8_C(u32 port, u32 val);
void x86_port_hanle16_C(u32 port, u32 val);
void x86_port_hanle32_C(u32 port, u32 val);

#ifdef __cplusplus
}
#endif

#endif
