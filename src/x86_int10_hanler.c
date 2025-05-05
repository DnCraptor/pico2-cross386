#include <string.h>
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
    switch(AL) {
        case 0:
            current_video_mode = AL;
            current_video_mode_width = 40;
            current_video_mode_height = 25;
            graphics_set_mode(TEXTMODE_53x30);
            break;
        case 3:
            if (!SELECT_VGA) goto e;
            current_video_mode = AL;
            current_video_mode_width = 80;
            current_video_mode_height = 25;
            graphics_set_mode(TEXTMODE_DEFAULT);
            break;
        default:
    }
e:
    VGA_FRAMBUFFER_WINDOW_SIZE = current_video_mode_width * current_video_mode_height * 2;
    u8* BDA = X86_FAR_PTR(0x0040, 0x0064);
    *BDA = current_video_mode; // 0x0464	1 байт	Режим работы видеоадаптера (номер режима)
    u16* BDA16 = (u16*)(BDA + 1); // 465h
    BDA16[0] = current_video_mode_width; // 0x0465	2 байта	Количество столбцов (ширина экрана в символах)
    BDA16[1] = 0xB800; // 0x0467	2 байта	Начальный адрес видеобуфера для скроллинга
    BDA16[2] = current_video_mode_height - 1; // 0x0469	2 байта	Количество строк на экране (минус 1)
    BDA16[3] = current_video_mode_width * 2; // 0x046B	2 байта	Количество байт на строку
    clrScr(7, 0);
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
    text_cursor_row = DH;
    text_cursor_column = DL;
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

/**
 * VIDEO - SELECT ACTIVE DISPLAY PAGE
 * AH = 05h
 * AL = new page number (00h to number of pages - 1) (see #00010)
 * Returns: Nothing
 */
inline static u32 x86_int10_hanler_05(u32 eax) {
    if (AL > 7) return 0;
    // 0x0460 — 2 байта, активная страница дисплея (значение указывает на текущую страницу видео, например, для VGA/CGА).
    u16* BDA = (u16*)X86_FAR_PTR(0x0040, 0x0060);
    *BDA = AL;
    text_page = AL;
    return 0;
}

/*
Вот список стандартных цветов, которые можно использовать в текстовом режиме на видеокартах типа CGA, EGA, VGA и других совместимых с 16-цветовой палитрой. Каждый цвет можно задать как значение от 0 до 15 (в 4-битном формате, где 4 старших бита — это цвет фона, а 4 младших — цвет текста).

### Стандартные цвета:
| Код | Цвет        | Описание                |
|-----|-------------|-------------------------|
| 0   | Черный      | Black                   |
| 1   | Синий       | Blue                    |
| 2   | Зеленый     | Green                   |
| 3   | Голубой     | Cyan                    |
| 4   | Красный     | Red                     |
| 5   | Фиолетовый  | Magenta                 |
| 6   | Желтый      | Yellow                  |
| 7   | Светло-серый| Light Gray              |
| 8   | Темно-серый | Dark Gray               |
| 9   | Светло-синий| Light Blue              |
| 10  | Светло-зеленый | Light Green          |
| 11  | Светло-голубой | Light Cyan           |
| 12  | Светло-красный | Light Red            |
| 13  | Светло-фиолетовый | Light Magenta     |
| 14  | Светло-желтый | Light Yellow          |
| 15  | Белый       | White                   |

### Пример использования в **BH**:
- **BH = 0x1F**:
  - Цвет текста: **0x1** (Белый)
  - Цвет фона: **0xF** (Черный)

### Формат записи:
- **Текст**: указывается в старших 4 битах (0x0-0xF).
- **Фон**: указывается в младших 4 битах (0x0-0xF). 

Это значение будет использоваться для записи пустых строк внизу окна после прокрутки (при использовании прерывания **AH = 06h**).
*/

/**
 * VIDEO - SCROLL UP WINDOW
 * AH = 06h
 * AL = number of lines by which to scroll up (00h = clear entire window)
 * BH = attribute used to write blank lines at bottom of window
 *      Низкие 4 бита (0-3): Атрибут фона (цвет фона). Высокие 4 бита (4-7): Атрибут текста (цвет текста).
 * CH,CL = row,column of window's upper left corner
 * DH,DL = row,column of window's lower right corner
 * Returns: Nothing
 */
inline static u32 x86_int10_hanler_06(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    // Получение атрибутов фона и текста из BH
    const uint16_t attr = ((BH & 0x0F) << 4) | (BH >> 4);  // swap colors

    const uint16_t width = current_video_mode_width;
    const uint16_t height = current_video_mode_height;

    // Смещение в видеопамяти для текущей страницы
    uint8_t* b1 = (uint8_t*)VGA_FRAMBUFFER_WINDOW_START;
    b1 += text_page * VGA_FRAMBUFFER_WINDOW_SIZE;

    const uint16_t blank_char = (attr << 8) | ' ';  // Пробел с атрибутами

    if (!AL) {
        u16* t_buf = (u16*)b1;
        int size = width * height;
        while (size--) *t_buf++ = blank_char;
        return 0;
    }

    // Прокрутка содержимого окна
    const uint8_t* window_start = b1 + (CH * width + CL) * 2;  // Начало окна (с учетом позиционирования)
    const uint8_t* window_end = b1 + (DH * width + DL) * 2;    // Конец окна (с учетом позиционирования)

    const uint16_t window_height = DH - CH + 1;  // Высота окна
    const uint16_t scroll_lines = AL;  // Количество строк для прокрутки

    // Прокручиваем строки в пределах окна
    if (scroll_lines > 0) {
        const uint8_t* src = window_start + (scroll_lines * width * 2); // Источник для копирования
        const uint8_t* dest = window_start;  // Куда копировать
        size_t sz = width * 2 * (window_height - scroll_lines);
        memcpy(dest, src, sz);  // Копирование области
        // Очистка нижней части окна
        u16* clear_area = (u16*)dest + sz;
        for (uint16_t row = 0; row < scroll_lines; ++row) {
            for (uint16_t col = 0; col < width; ++col) {
                *clear_area++ = blank_char;
            }
        }
    }
    return 0;
}

/**
 * VIDEO - SCROLL DOWN WINDOW
 * AH = 07h
 * AL = number of lines by which to scroll down (00h = clear entire window)
 * BH = attribute used to write blank lines at top of window
 *      Низкие 4 бита (0-3): Атрибут фона (цвет фона). Высокие 4 бита (4-7): Атрибут текста (цвет текста).
 * CH,CL = row,column of window's upper left corner
 * DH,DL = row,column of window's lower right corner
 * Returns: Nothing
 */
inline static u32 x86_int10_hanler_07(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    // Получение атрибутов фона и текста из BH
    const uint16_t attr = ((BH & 0x0F) << 4) | (BH >> 4);  // swap colors

    const uint16_t width = current_video_mode_width;
    const uint16_t height = current_video_mode_height;

    // Смещение в видеопамяти для текущей страницы
    uint8_t* b1 = (uint8_t*)VGA_FRAMBUFFER_WINDOW_START;
    b1 += text_page * VGA_FRAMBUFFER_WINDOW_SIZE;

    const uint16_t blank_char = (attr << 8) | ' ';  // Пробел с атрибутами

    if (!AL) {
        u16* t_buf = (u16*)b1;
        int size = width * height;
        while (size--) *t_buf++ = blank_char;
        return 0;
    }

    // Прокрутка содержимого окна
    const uint8_t* window_start = b1 + (CH * width + CL) * 2;  // Начало окна (с учетом позиционирования)
    const uint8_t* window_end = b1 + (DH * width + DL) * 2;    // Конец окна (с учетом позиционирования)

    const uint16_t window_height = DH - CH + 1;  // Высота окна
    const uint16_t scroll_lines = AL;  // Количество строк для прокрутки

    // Прокручиваем строки в пределах окна
    if (scroll_lines > 0) {
        const uint8_t* src = window_end - (scroll_lines * width * 2); // Источник для копирования
        uint8_t* dest = window_end - (scroll_lines * width * 2);  // Куда копировать
        size_t sz = width * 2 * (window_height - scroll_lines);
        memcpy(dest, src, sz);  // Копирование области
        // Очистка верхней части окна
        u16* clear_area = (u16*)window_start;
        for (uint16_t row = 0; row < scroll_lines; ++row) {
            for (uint16_t col = 0; col < width; ++col) {
                *clear_area++ = blank_char;
            }
        }
    }
    return 0;
}

/**
 * VIDEO - READ CHARACTER AND ATTRIBUTE AT CURSOR POSITION
 * AH = 08h
 * BH = page number (00h to number of pages - 1) (see #00010)
 * 
 * Returns:
 * AH = character's attribute (text mode only) (see #00014)
 * AL = character
 (Table 00015)
Values for character color:.
Normal          Bright
000b   black           dark gray
001b   blue            light blue
010b   green           light green
011b   cyan            light cyan
100b   red             light red
101b   magenta         light magenta
110b   brown           yellow
111b   light gray      white
 */
inline static u32 x86_int10_hanler_08(u32 ebx) {
    if (BH > 7) return 0;
    u16* BDA = (u16*)X86_FAR_PTR(0x0040, 0x0050);
    u16 edx = BDA[BH];
    // DH = row (00h is top)
    // DL = column (00h is left)
    uint8_t* b1 = (uint8_t*)VGA_FRAMBUFFER_WINDOW_START;
    b1 += BH * VGA_FRAMBUFFER_WINDOW_SIZE;
    uint16_t* b16 = (uint16_t*)b1;
    return b16[current_video_mode_width * DH + DL];
}

/**
 * VIDEO - WRITE CHARACTER AND ATTRIBUTE AT CURSOR POSITION
 * AH = 09h
 * AL = character to display
 * BH = page number (00h to number of pages - 1) (see #00010)
 * background color in 256-color graphics modes (ET4000)
 * BL = attribute (text mode) or color (graphics mode)
 * if bit 7 set in <256-color graphics mode, character is XOR'ed onto screen
 * CX = number of times to write character
 * Returns: Nothing
 */
inline static u32 x86_int10_hanler_09(u32 eax, u32 ebx, u32 ecx) {
    if (BH > 7) return 0;
    u16* BDA = (u16*)X86_FAR_PTR(0x0040, 0x0050);
    u16 edx = BDA[BH];
    u16 row = DH;
    u16 column = DL;
    u8* b1 = (u8*)VGA_FRAMBUFFER_WINDOW_START;
    b1 += BH * VGA_FRAMBUFFER_WINDOW_SIZE;
    u16 width = current_video_mode_width;
    u16 heigh = current_video_mode_height;
    u16* b16 = (u16*)b1;
    b16 += width * row + column;
    u16 c = ((u16)BL << 8) | AL;
    for (u16 i = 0; i < CX; ++i) {
        *b16++ = c;
        ++column;
        if (column >= width) {
            column = 0;
            ++row;
        }
        if (row >= heigh) {
            break;
        }
    }
    BDA[BH] = (row << 8) | column;
    text_cursor_row = row;
    text_cursor_column = column;
    return 0;
}

/**
 * VIDEO - WRITE CHARACTER ONLY AT CURSOR POSITION
 * AH = 0Ah
 * AL = character to display
 * BH = page number (00h to number of pages - 1) (see #00010)
 * background color in 256-color graphics modes (ET4000)
 * BL = attribute (PCjr, Tandy 1000 only) or color (graphics mode)
 * if bit 7 set in <256-color graphics mode, character is XOR'ed onto screen
 * CX = number of times to write character
 * Returns: Nothing
 */
inline static u32 x86_int10_hanler_0A(u32 eax, u32 ebx, u32 ecx) {
    if (BH > 7) return 0;
    u16* BDA = (u16*)X86_FAR_PTR(0x0040, 0x0050);
    u16 edx = BDA[BH];
    u16 row = DH;
    u16 column = DL;
    u8* b1 = (u8*)VGA_FRAMBUFFER_WINDOW_START;
    b1 += BH * VGA_FRAMBUFFER_WINDOW_SIZE;
    u16 width = current_video_mode_width;
    u16 heigh = current_video_mode_height;
    u16* b16 = (u16*)b1;
    b16 += width * row + column;
    for (u16 i = 0; i < CX; ++i) {
        u16 c = (*b16 & 0xFF00) | AL;
        *b16++ = c;
        ++column;
        if (column >= width) {
            column = 0;
            ++row;
        }
        if (row >= heigh) {
            break;
        }
    }
    BDA[BH] = (row << 8) | column;
    text_cursor_row = row;
    text_cursor_column = column;
    return 0;
}

inline static u32 x86_int10_hanler_0B(u32 ebx) {
    switch(BH) {
        case 0: // SET BACKGROUND/BORDER COLOR
            border_color = BL;
            return 0;
        case 1: // This call was only valid in 320x200 graphics on the CGA, but newer cards support it in many or all graphics modes
            /**
             * BL = palette ID
             * 00h background, green, red, and brown/yellow
             * 01h background, cyan, magenta, and white
             */
            paletteID = BL;
            return 0;
    }
    return 0xFF00 | CF_ON;
}

/**
 * VIDEO - TELETYPE OUTPUT
 * AH = 0Eh
 * AL = character to write
 * BH = page number
 * BL = foreground color (graphics modes only)
 * Return: Nothing
 * Desc: Display a character on the screen, advancing the cursor and scrolling the screen as necessary
 */
inline static u32 x86_int10_hanler_0E(u32 eax, u32 ebx) {
    if (BH > 7) return 0;

    // Получить позицию курсора из BDA
    u16* BDA = (u16*)X86_FAR_PTR(0x0040, 0x0050);
    u16 edx = BDA[BH];
    if (AL == 0x0A) {
        // Сохранить новую позицию курсора
        BDA[BH] = (DH << 8);
        text_cursor_row = DH;
        text_cursor_column = 0;
        return 0;
    }
    u16 row = DH;
    u16 column = DL;

    // Базовый адрес видеопамяти для страницы
    u8* b1 = (u8*)VGA_FRAMBUFFER_WINDOW_START;
    b1 += BH * VGA_FRAMBUFFER_WINDOW_SIZE;

    u16 width = current_video_mode_width;
    u16 height = current_video_mode_height;
    u16* b16 = (u16*)b1;
    b16 += width * row + column;

    // Записать символ
    u16 c = (*b16 & 0xFF00) | AL;  // сохранить атрибут, заменить символ
    if (AL == 0x0D) goto nextLine;
    *b16 = c;

    // Переместить курсор вправо
    ++column;
    if (column >= width) {
nextLine:
        column = 0;
        ++row;
        if (row >= height) {
            row = height - 1;
            // Скроллинг: сдвинуть всю память экрана на одну строку вверх
            u16* vram = (u16*)b1;
            memcpy(vram, vram + width, (height - 1) * width * sizeof(u16));
            // Очистить последнюю строку пробелами с атрибутом
            u16 blank = (c & 0xFF00) | ' ';
            u16* last_line = vram + (height - 1) * width;
            for (u16 i = 0; i < width; ++i) {
                last_line[i] = blank;
            }
        }
    }

    // Сохранить новую позицию курсора
    BDA[BH] = (row << 8) | column;
    text_cursor_row = row;
    text_cursor_column = column;
    return 0;
}

/**
 * VIDEO - GET CURRENT VIDEO MODE
 * AH = 0Fh
 * 
 * Returns:
 * AH = number of character columns
 * AL = display mode (see #00010 at AH=00h)
 * BH = active page (see AH=05h)
 * Notes: If mode was set with bit 7 set ("no blanking"), the returned mode will also have bit 7 set. EGA, VGA, and UltraVision return either AL=03h (color) or AL=07h (monochrome) in all extended-row text modes. HP 200LX returns AL=07h (monochrome) if mode was set to AL=21h and always 80 resp. 40 columns in all text modes regardless of current zoom setting (see AH=D0h). When using a Hercules Graphics Card, additional checks are necessary:

mode 05h:
If WORD 0040h:0063h is 03B4h, may be in graphics page 1
(as set by DOSSHELL and other Microsoft software)

mode 06h:
If WORD 0040h:0063h is 03B4h, may be in graphics page 0
(as set by DOSSHELL and other Microsoft software)

mode 07h:
If BYTE 0040h:0065h bit 1 is set, Hercules card is in
graphics mode, with bit 7 indicating the page (mode set by
Hercules driver for Borland Turbo C).
The Tandy 2000 BIOS is only documented as returning AL, not AH or BH
 */
inline static u32 x86_int10_hanler_0F(void) {
    register u32 eax = (current_video_mode_width << 8) | current_video_mode;
    u32 ebx = text_page;
    __asm volatile ("mov r5, %0" :: "r"(ebx): "r5");
    return eax;
}

u32 x86_int10_hanler_C(u32 eax, u32 ebx, u32 ecx, u32 edx) {
   // goutf(30 - 1, false, "x86_int10_hanler_C(%08X, %08X, %08X, %08X)", eax, ebx, ecx, edx);
    switch (AH) { // AH - function
        case 0:
            return x86_int10_hanler_00(eax);
        case 1:
            return x86_int10_hanler_01(ecx);
        case 2:
            return x86_int10_hanler_02(ebx, edx);
        case 3:
            return x86_int10_hanler_03(ebx);
        case 5:
            return x86_int10_hanler_05(eax);
        case 6:
            return x86_int10_hanler_06(eax, ebx, ecx, edx);
        case 7:
            return x86_int10_hanler_07(eax, ebx, ecx, edx);
        case 8:
            return x86_int10_hanler_08(ebx);
        case 9:
            return x86_int10_hanler_09(eax, ebx, ecx);
        case 0x0A:
            return x86_int10_hanler_0A(eax, ebx, ecx);
        case 0x0B:
            return x86_int10_hanler_0B(ebx);
        case 0x0E:
            return x86_int10_hanler_0E(eax, ebx);
        case 0x0F:
            return x86_int10_hanler_0F();
    }
    return 0xFF00 | CF_ON;
}
