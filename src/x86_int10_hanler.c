#include <pico/stdlib.h>
#include "x86.h"
#include "graphics.h"

/*
Для **минимальной** работы DOS с экраном через `INT 10h` нужны далеко не все функции BIOS-видео.

Вот что **обязательно нужно**, чтобы DOS нормально работал:

---

### Минимально необходимые функции `INT 10h`:

| AH | Функция                        | Назначение                         |
|----|---------------------------------|------------------------------------|
| 00h | Set Video Mode                 | Установить видеорежим (например, 03h — текст 80x25) |
| 01h | Set Cursor Type                | Задать форму курсора              |
| 02h | Set Cursor Position            | Установить позицию курсора         |
| 03h | Read Cursor Position           | Прочитать позицию курсора          |
| 06h | Scroll Window Up               | Прокрутка окна вверх              |
| 07h | Scroll Window Down             | Прокрутка окна вниз               |
| 08h | Read Character and Attribute at Cursor | Прочитать символ и атрибут в позиции курсора |
| 09h | Write Character and Attribute at Cursor | Печать символа с атрибутом (без перемещения курсора) |
| 0Ah | Write Character Only at Cursor | Печать символа без атрибута        |
| 0Eh | Teletype Output                 | Вывод символа на экран и продвижение курсора (используется для обычного вывода текста!) |
| 0Fh | Get Current Video Mode          | Прочитать текущий видеорежим (режим, страницы, ширина экрана) |

---

### Почему именно они:
- **Set/Get Video Mode (00h, 0Fh)** — чтобы DOS мог узнать, что за экран и правильно его использовать.
- **Set/Get Cursor (01h, 02h, 03h)** — DOS много работает с позиционированием курсора.
- **Teletype Output (0Eh)** — основной способ "вывести текст" в стандартном текстовом режиме BIOS.
- **Scroll Up/Down (06h, 07h)** — для прокрутки текста (например, когда экран заполняется).
- **Write/Read Character (08h, 09h, 0Ah)** — при продвинутой печати и редакторах.

> Пример: команды типа `DIR` используют Teletype Output (0Eh) + прокрутку экрана (06h), а при редактировании строк курсор двигают через (02h).

---

### Что можно не поддерживать:
- Функции работы с палитрами, графикой, шрифтами (10h, 11h, 12h и т.п.)
- Многостраничность видео (если игнорировать AH=05h "Set Active Page", пока не требуется).

---
Минимальные видеорежимы для работы DOS:

Режим (AL)	Описание	Нужно ли
03h	Текстовый 80×25, 16 цветов	Обязательно
00h	Текстовый 40×25, 16 цветов	Желательно, но можно пропустить
01h	Текстовый 40×25, 16 цветов (широкий шрифт)	Необязательно
02h	Текстовый 80×25, 16 цветов (широкий шрифт)	Необязательно

*/

/**
 * VIDEO - SET VIDEO MODE
 * AH = 00h
 * AL = desired video mode (see #00010)
 * 
 * Return:
 * AL = video mode flag (Phoenix, AMI BIOS)
 * 20h mode > 7
 * 30h modes 0-5 and 7
 * 3Fh mode 6
 * AL = CRT controller mode byte (Phoenix 386 BIOS v1.10)
 */
inline static u32 x86_int10_hanler_00(u32 eax) {
    // TODO:
    /*
if (mode <= 5 || mode == 7) {
    AL = 0x30;    // текстовые режимы 0-5, 7
} else if (mode == 6) {
    AL = 0x3F;    // режим 6 (640x200 монохромная графика)
} else {
    AL = 0x20;    // все остальные режимы >7
}
    */
    clrScr(0);
    return 0x30; 
}

/**
 * VIDEO - SET TEXT-MODE CURSOR SHAPE
 * AH = 01h
 * CH = cursor start and options (see #00013)
 * CL = bottom scan line containing cursor (bits 0-4)
 * Returns: Nothing
 * 
 * Bitfields for cursor start and options:
 * Bit(s)  Description     (Table 00013)
 * 7      should be zero
 * 6,5    cursor blink.
 * (00=normal, 01=invisible, 10=erratic, 11=slow).
 * (00=normal, other=invisible on EGA/VGA)
 * 4-0    topmost scan line containing cursor
 */
inline static u32 x86_int10_hanler_01(u32 ecx) {
    text_cursor_type = (u16)ecx;
    return 0;
}

/**
 * VIDEO - SET CURSOR POSITION
 * AH = 02h
 * BH = page number
 * 0-3 in modes 2&3
 * 0-7 in modes 0&1
 * 0 in graphics modes
 * DH = row (00h is top)
 * DL = column (00h is left)
 * Returns: Nothing
 */
inline static u32 x86_int10_hanler_02(u32 ebx, u32 edx) {
    if (BH > 7) return 0;
    u16* BDA = (u16*)X86_FAR_PTR(0x0040, 0x0050);
    BDA[BH] = (u16)edx;
    // TODO: uint8_t text_cursor_row = 0;
//uint8_t text_cursor_column = 0;
    return 0;
}

/**
 * VIDEO - GET CURSOR POSITION AND SIZE
 * AH = 03h
 * BH = page number
 * 0-3 in modes 2&3
 * 0-7 in modes 0&1
 * 0 in graphics modes
 * 
 * Return:
 * AX = 0000h (Phoenix BIOS)
 * CH = start scan line
 * CL = end scan line
 * DH = row (00h is top)
 * DL = column (00h is left)
 */
inline static u32 x86_int10_hanler_03(u32 ebx) {
    if (BH > 7) return 0;
    u16* BDA = (u16*)X86_FAR_PTR(0x0040, 0x0050);
    u32 edx = BDA[BH];
    // Используем сохранённое значение text_cursor_type для извлечения данных курсора
    // Распаковываем text_cursor_type, чтобы извлечь верхнюю строку и нижнюю строку
    u16 cursor = text_cursor_type; // Это значение от AH = 01h
    u32 topmost_scan_line = cursor & 0x1F; // Верхняя строка курсора (бит 0-4)
//    u8 cursor_blink = (cursor >> 5) & 0x03; // Режим мигания курсора (бит 5-6)
    u32 bottom_scan_line = (cursor >> 8) & 0x1F; // Нижняя строка курсора (бит 0-4 в CL)
    // Теперь заполняем регистры CH и CL для запроса AH = 03h
    u32 ecx = (topmost_scan_line << 8) | bottom_scan_line;

    __asm volatile ("mov r6, %0" :: "r"(ecx): "r6");
    __asm volatile ("mov r7, %0" :: "r"(edx): "r7");
    return 0;
}

u32 x86_int10_hanler_C(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    goutf(current_video_mode_height - 1, false, "x86_int10_hanler_C(%08X, %08X, %08X, %08X)", eax, ebx, ecx, edx);
    switch (AH) { // AH - function
        case 0:
            return x86_int10_hanler_00(eax);
        case 1:
            return x86_int10_hanler_01(ecx);
        case 2:
            return x86_int10_hanler_02(ebx, edx);
        case 3:
            return x86_int10_hanler_03(ebx);
    }
    return 0;
}
