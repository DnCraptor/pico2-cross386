#include "ports.h"
#include "8259A.h"

u8 X86_PORTS[64 << 10] = { 0 };

void x86_port_hanle8_C(u32 port, u32 val) {
    X86_PORTS[(u16)port] = (u8)val;
    switch (port) {
        case 0x20: // Master PIC
            x86_8259A_Master_PIC(val);
            break;
        case 0xA0: // Slave PIC
            x86_8259A_Slave_PIC(val);
            break;
        case 0x21: // Master PIC
            x86_8259A_Master_PIC_D(val);
            break;
        case 0xA1: // Slave PIC
            x86_8259A_Slave_PIC_D(val);
            break;
    }
}

void x86_port_hanle16_C(u32 port, u32 val) {
    x86_port_hanle8_C(port++, val);
    x86_port_hanle8_C(port, val >> 8);
}

void x86_port_hanle32_C(u32 port, u32 val) {
    x86_port_hanle8_C(port++, val);
    x86_port_hanle8_C(port++, val >> 8);
    x86_port_hanle8_C(port++, val >> 16);
    x86_port_hanle8_C(port, val >> 24);
}
