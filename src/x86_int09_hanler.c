#include "x86.h"
#include "ports.h"
#include "graphics.h"

void x86_int09_hanler_C(void) {
    x86_bios_process_key(X86_PORTS[0x60]);
    X86_PORTS[0x62] &= ~2;
}
