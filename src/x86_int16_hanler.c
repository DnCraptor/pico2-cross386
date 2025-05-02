#include "x86.h"
#include "graphics.h"

/**
 * KEYBOARD - GET KEYSTROKE
 * AH = 00h
 * 
 * Returns:
 * AH = BIOS scan code
 * AL = ASCII character
 * 
 * Notes: On extended keyboards, this function discards any extended keystrokes, returning only when a non-extended keystroke is available. The BIOS scan code is usually, but not always, the same as the hardware scan code processed by INT 09. It is the same for ASCII keystrokes and most unshifted special keys (F-keys, arrow keys, etc.), but differs for shifted special keys. Some (older) clone BIOSes do not discard extended keystrokes and manage function AH=00h and AH=10h the same. The K3PLUS v6.00+ INT 16 BIOS replacement doesn't discard extended keystrokes (same as with functions 10h and 20h), but will always translate prefix E0h to 00h. This allows old programs to use extended keystrokes and should not cause compatibility problems
 */
inline static u32 x86_int16_00() {
    return x86_dequeue_key(1, 0);
}

/**
 * KEYBOARD - CHECK FOR KEYSTROKE
 * AH = 01h
 * 
 * Return:
 * ZF set if no keystroke available
 * ZF clear if keystroke available
 * AH = BIOS scan code
 * AL = ASCII character
 * 
 * Note: If a keystroke is present, it is not removed from the keyboard buffer; however, any extended keystrokes which are not compatible with 83/84- key keyboards are removed by IBM and most fully-compatible BIOSes in the process of checking whether a non-extended keystroke is available. Some (older) clone BIOSes do not discard extended keystrokes and manage function AH=00h and AH=10h the same. The K3PLUS v6.00+ INT 16 BIOS replacement doesn't discard extended keystrokes (same as with functions 10h and 20h), but will always translate prefix E0h to 00h. This allows old programs to use extended keystrokes and should not cause compatibility problems
 */
inline static u32 x86_int16_01() {
    return x86_dequeue_key(0, 0);
}

/**
 * KEYBOARD - GET SHIFT FLAGS
 * AH = 02h
 * 
 * Returns:
 * AL = shift flags (see #00582)
 * AH destroyed by many BIOSes
 * 
 * See Also: AH=12h - AH=22h - INT 17/AH=0Dh - INT 18/AH=02h
 * 
 * Bitfields for keyboard shift flags:
Bit(s)  Description     (Table 00582)
7      Insert active
6      CapsLock active
5      NumLock active
4      ScrollLock active
3      Alt key pressed (either Alt on 101/102-key keyboards)
2      Ctrl key pressed (either Ctrl on 101/102-key keyboards)
1      left shift key pressed
0      right shift key pressed
See Also: #00587 - #03743 - MEM 0040h:0017h - #M0010
 */
inline static u32 x86_int16_02() {
    return BDA->kbd_flag0;
}

u32 x86_int16_hanler_C(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    switch (AH) { // AH - function
        case 0: // KEYBOARD - GET KEYSTROKE
            return x86_int16_00();
        case 1: // KEYBOARD - CHECK FOR KEYSTROKE
            return x86_int16_01();
        case 2: // KEYBOARD - GET SHIFT FLAGS
            return x86_int16_02();
        case 0x05:
            return !x86_enqueue_key(CX);
    // bit Bochs Description
    //  7    0   reserved
    //  6    0   INT 16/AH=20h-22h supported (122-key keyboard support)
    //  5    1   INT 16/AH=10h-12h supported (enhanced keyboard support)
    //  4    1   INT 16/AH=0Ah supported
    //  3    0   INT 16/AX=0306h supported
    //  2    0   INT 16/AX=0305h supported
    //  1    0   INT 16/AX=0304h supported
    //  0    0   INT 16/AX=0300h supported
        case 0x09:
            return 0x30;
        // GET KEYBOARD ID
        case 0x0a:
            // TODO:
            __asm volatile ("ldr r5, =0" ::); // R5 ← BX (BH:BL packed)
            return 0;
        // read MF-II keyboard input
        case 0x10:
            x86_dequeue_key(1, 1);
            return 0;
        // check MF-II keyboard status
        case 0x11:
            x86_dequeue_key(0, 1);
            return 0;
        case 0x12:
            return ((GET_BDA(kbd_flag0) & ~((KF1_RCTRL|KF1_RALT) << 8)) | ((GET_BDA(kbd_flag1) & (KF1_RCTRL|KF1_RALT)) << 8));
        case 0x92: return 0x80 << 8;
        case 0x6f: return AL == 0x08 ? (2 << 8) : 0;
        case 0xa2: return eax;
        
    }
    return 0x8600 | CF_ON;
}
