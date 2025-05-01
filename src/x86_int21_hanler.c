#include "x86.h"
#include "ports.h"
#include "graphics.h"

void x86_int21_hanler_C(void) {
    //goutf(30-1, false, "W %02X", X86_PORTS[0x60]);
    x86_bios_process_key(X86_PORTS[0x60]);
    X86_PORTS[0x62] &= ~2;
}
