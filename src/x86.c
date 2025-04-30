#include <string.h>
#include "x86.h"
#include "ports.h"
#include "graphics.h"

u16 X86_CS = 0xF000;
u16 X86_DS = 0;
u16 X86_ES = 0;
u16 X86_FS = 0;
u16 X86_GS = 0;
u16 X86_SS = 0;
u32 X86_CR0 = 0x00000010; // PE=0, ET=1

static void ints(void) {
    // init interrupts
    volatile uint32_t* X86_BASE_RAM = (uint32_t*)X86_RAM_BASE;
    for (int i = 0; i < 256; ++i) {
        X86_BASE_RAM[i] = (uint32_t)&x86_iret;
    }
    // TODO:
    X86_BASE_RAM[0x10] = (uint32_t)&x86_int10_hanler;
    X86_BASE_RAM[0x11] = (uint32_t)&x86_int11_hanler;
    X86_BASE_RAM[0x12] = (uint32_t)&x86_int12_hanler;
    X86_BASE_RAM[0x13] = (uint32_t)&x86_int13_hanler;
    X86_BASE_RAM[0x15] = (uint32_t)&x86_int15_hanler;
    X86_BASE_RAM[0x16] = (uint32_t)&x86_int16_hanler;
    X86_BASE_RAM[0x21] = (uint32_t)&x86_int21_hanler;
}

/**
 * 0x20 = IRQ0 (таймер),
 * 0x21 = IRQ1 (клавиатура),
 * ...,
 * 0x2F = IRQ15 (всего 16 IRQ)
 */
static void irqs(void) {
    // Master PIC
    x86_port_hanle8_C(0x20, 0x11); // ICW1: Начинаем инициализацию, ожидаем ICW4
// XT case    x86_port_hanle8_C(0x21, 0x08); // ICW2: Базовый вектор прерываний 08h
    x86_port_hanle8_C(0x21, 0x20); // ICW2: Базовый вектор прерываний 20h
    x86_port_hanle8_C(0x21, 0x04); // ICW3: Подчиненный контроллер на IRQ2
    x86_port_hanle8_C(0x21, 0x01); // ICW4: Режим 8086/88
    // Slave PIC
    x86_port_hanle8_C(0xA0, 0x11); // ICW1: Начинаем инициализацию, ожидаем ICW4
// XT case    x86_port_hanle8_C(0xA1, 0x08); // ICW2: Базовый вектор прерываний 08h
    x86_port_hanle8_C(0xA1, 0x28); // ICW2: Базовый вектор прерываний 28h
    x86_port_hanle8_C(0xA1, 0x02); // ICW3: Подключен к IRQ2 на master (bit 2)
    x86_port_hanle8_C(0xA1, 0x01); // ICW4: Режим 8086/88

    x86_port_hanle8_C(0x21, 0xFD); // 11111101b — разрешен только IRQ1
    x86_port_hanle8_C(0xA1, 0xFF); // Все IRQ на slave отключены
}

static void post(void) {
    // POST test results:
    u8* BDA = X86_FAR_PTR(0x0040, 0x0000);
    u16* BDA16 = (u16*)BDA;
    for (int i = 0; i < 128; ++i) {
        BDA16[i] = 0;
    }
    /*  Расшифровка Equipment List (0x0410):
    Бит	Описание
    0-1	Количество дискетных приводов - 1 меньше реального количества:
    00 = 1 дискета, 01 = 2 дискеты, 10 = 3, 11 = 4
    2	1 = Есть матричный принтер (параллельный порт с принтером)
    3	1 = Имеется системный видеоадаптер (например, CGA, EGA)
    4-5	Тип начального видеоадаптера:
    00 = EGA/VGA, 01 = 40×25 текстовый цветной, 10 = 80×25 текстовый цветной, 11 = монохром
    6	1 = Имеется DMA-контроллер
    7	1 = Имеется математический сопроцессор (FPU, например, 8087)
    8-11	Количество параллельных портов:
    0 = нет портов, 1 = один порт и т.д.
    12-15	Количество последовательных портов:
    0 = нет портов, 1 = один порт и т.д.
    */
    BDA16[8] = SELECT_VGA ? 0b001001 : 0b011001;
    BDA16[8] = 0; /// TODO: 0x1234 = перезапуск, 0x0000 = холодный старт

    u16* bda16 = (u16*)(BDA + 0x13);
    *bda16 = 640; // 0x0413	Размер конвенциональной памяти (в килобайтах)
    
    //1Ah	WORD	Keyboard: ptr to next character in keyboard buffer
    //1Ch	WORD	Keyboard: ptr to first free slot in keyboard buffer
    //1Eh 16 WORDs	Keyboard circular buffer (but see 80h, 82h for override)

    // 0x0445	2 байта	Таймаут для автоповтора клавиатуры
    // 0x0447	2 байта	Таймаут клавиатуры после последней активности
    // 0x0449	2 байта	Количество нажатий клавиши Ctrl+Alt+Del
    // 0x044B	2 байта	Длина текущего таймаута POST'а
    // 0x044D	1 байт	Номер последней активной дискеты + 0x044E	1 байт	Статус дискеты (флаги, ошибок и пр.)
    // 0x044F	1 байт	Таймер моторчика дискеты
    // 0x0450	16 байт	Позиции курсора для всех видео страниц
    BDA16 = (u16*)(BDA + 0x60);
    // 0x0460	2 байта	Активная страница дисплея
    BDA16[1] = 0xB800; // 0x0462	2 байта	Базовый адрес видеобуфера (0xB800, 0xB000)
    BDA = X86_FAR_PTR(0x0040, 0x0064);
    *BDA = current_video_mode; // 0x0464	1 байт	Режим работы видеоадаптера (номер режима)
    BDA16 = (u16*)(BDA + 1); // 465h
    BDA16[0] = current_video_mode_width; // 0x0465	2 байта	Количество столбцов (ширина экрана в символах)
    BDA16[1] = 0xB800; // 0x0467	2 байта	Начальный адрес видеобуфера для скроллинга
    BDA16[2] = current_video_mode_height - 1; // 0x0469	2 байта	Количество строк на экране (минус 1)
    BDA16[3] = current_video_mode_width * 2; // 0x046B	2 байта	Количество байт на строку
    BDA16[4] = 0x50; // 0x046D	2 байта	Адрес первого свободного сегмента памяти
    BDA = X86_FAR_PTR(0x0040, 0x006F);
    *BDA = 0; // 0x046F	1 байт	Режим дисплея: цветной/монохром
    BDA16 = (u16*)(BDA + 1); // 470h
    BDA16[0] = 4; // 0x0470	2 байта	Количество установленных жестких дисков
    BDA16[1] = 0x1234; // 0x0472	2 байта	Завершение POST (значение 0x1234h при успехе)
}

void x86_init(void) {
    X86_CS = 0xF000;
    X86_DS = 0;
    X86_ES = 0;
    X86_FS = 0;
    X86_GS = 0;
    X86_SS = 0;
    X86_CR0 = 0x00000010; // PE=0, ET=1
    ints();
    irqs();
    post();
}

void x86_add_char_to_BDA(u8 scan, u8 ascii) {
    // Получаем указатель на BDA (Base Data Area) для чтения данных клавиатуры
    const u8* BDA = X86_FAR_PTR(0x0040, 0x0000);
    u16* BDA16 = (u16*)(BDA + 0x1A);
    const u16 tail = BDA16[1] % 16; // Хвост указателя записи клавиатурного буфера (0x041C)
    // Буфер клавиш начинается с 0x041E, он имеет 16 слов (32 байта)
    u16* buffer = (u16*)(BDA + 0x1E);
    buffer[tail] = ((u16)scan << 8) | ascii;
    static int i = 0;
    goutf(30-1, false, "W %02X[%c]/%02X (%d)", ascii, ascii, scan, i++);
    BDA16[1] = tail + 1;
}

/**
 *  17h	BYTE	Keyboard status flags 1:
		    bit 7 =1 INSert active
		    bit 6 =1 Caps Lock active
		    bit 5 =1 Num Lock active
		    bit 4 =1 Scroll Lock active
		    bit 3 =1 either Alt pressed
		    bit 2 =1 either Ctrl pressed
		    bit 1 =1 Left Shift pressed
		    bit 0 =1 Right Shift pressed
 18h	BYTE	Keyboard status flags 2:
		    bit 7 =1 INSert pressed
		    bit 6 =1 Caps Lock pressed
		    bit 5 =1 Num Lock pressed
		    bit 4 =1 Scroll Lock pressed
		    bit 3 =1 Pause state active
		    bit 2 =1 Sys Req pressed
		    bit 1 =1 Left Alt pressed
		    bit 0 =1 Left Ctrl pressed
 */
void x86_update_kbd_BDA(u8 keyboard_status, u8 extended_status) {
    u8* BDA = X86_FAR_PTR(0x0040, 0x0000);
    // Сохранение статуса в BDA
    BDA[0x17] = keyboard_status;  // Сохраняем статус клавиатуры в BDA
    BDA[0x18] = extended_status;  // Сохраняем расширенный статус в BDA
}
