#include <string.h>
#include <stdarg.h>
#include "graphics.h"
#include "x86.h"

bool SELECT_VGA = true;
uint32_t current_video_mode = 3; // 80*25
uint32_t current_video_mode_width = 80;
uint32_t current_video_mode_height = 25;
uint8_t SCREEN[256l << 10]; // Emulate 256 KB of frame-buffer - real buffer
// in PSRAM:
uint32_t VGA_FRAMBUFFER_WINDOW_START = 0x11000000 + 0xB8000; // 0x03 video mode
uint32_t VGA_FRAMBUFFER_WINDOW_SIZE = 80 * 25 * 2; // 0x03 video mode
// 320x200 пикселей с 256 цветами (мод 0x13), 0xA0000, 320×200=64000 байт=0xF800 байт

/*
CH:
Bit(s)	Описание
7	Всегда должен быть равен 0 (не используется).
6, 5	Мигание курсора (Cursor Blink).
4-0	Топовая строка, содержащая курсор (Topmost scan line).
Мигание курсора (битовые 6 и 5):
00 — обычный курсор.
01 — курсор невидимый (invisible).
10 — курсор мигающий (erratic).
11 — курсор мигает медленно (slow).
Топовая строка, содержащая курсор (битовые 4–0):
Эти биты указывают на верхнюю строку (сканлайн) курсора в видеопамяти.
Например, если установлен бит 4-0 как 0x0F, это значит, что курсор будет находиться на строке с номером 15.

CL — указывает на нижнюю строку, где заканчивается курсор. Это строка, которая будет отображаться в видеопамяти как нижняя часть курсора.
Например, если в CL записано значение 0x04, это значит, что курсор будет находиться на строках 3–4.
CL имеет значения от 0 до 31, так как видеоэкран обычно состоит из 25 строк текста.
*/
uint32_t text_cursor_type = 0x1018; // мигающий курсор внизу строки.
uint8_t text_cursor_row = 0;
uint8_t text_cursor_column = 0;
uint8_t text_page = 0;
uint8_t border_color = 0; // background/border color (border only in text modes)
uint8_t paletteID = 0; // valid in 320x200 graphics on the CGA, but newer cards support it in many or all graphics modes

void __time_critical_func() handle_frame_changed() {
    uint8_t* b1 = (uint8_t*)VGA_FRAMBUFFER_WINDOW_START;
    b1 += text_page * VGA_FRAMBUFFER_WINDOW_SIZE;
    uint8_t* b2 = SCREEN;
    uint32_t w = SELECT_VGA ? (current_video_mode == 3 ? 80 : 40) : 53;
    if (current_video_mode == 0) { // HDMI 53*30 -> 40*25
        #ifdef CLEANUP_VGA_TOP_DEBUG_SPACE
        memset(b2, 0, w * 2 * 2); // first 2 lines
        #endif
        b2 += w * 2 * 2;
        if (w == 53) {
            for (int line = 0; line < 25; ++line) {
                memset(b2, 0, 6 * 2); // first 6 chars in line
                b2 += 6 * 2;
                memcpy(b2, b1, 40 * 2);
                b1 += 40 * 2;
                b2 += 40 * 2;
                memset(b2, 0, 7 * 2); // last 7 chars in line
                b2 += 7 * 2;
            }
        } else {
            memcpy(b2, b1, VGA_FRAMBUFFER_WINDOW_SIZE);
            b2 += VGA_FRAMBUFFER_WINDOW_SIZE;
        }
        #ifdef CLEANUP_VGA_BOTTOM_DEBUG_SPACE
        memset(b2, 0, w * 3 * 2); // last 3 lines
        #endif
    } else {
        #ifdef CLEANUP_VGA_TOP_DEBUG_SPACE
        memset(b2, 0, w * 2 * 2); // first 2 lines
        #endif
        b2 += w * 2 * 2;
        memcpy(b2, b1, VGA_FRAMBUFFER_WINDOW_SIZE);
        #ifdef CLEANUP_VGA_BOTTOM_DEBUG_SPACE
        b2 += VGA_FRAMBUFFER_WINDOW_SIZE;
        memset(b2, 0, w * 3 * 2); // last 3 lines
        #endif
    }
}

void draw_text(const char* string, uint32_t x, uint32_t y, uint8_t color, uint8_t bgcolor) {
    uint8_t* t_buf = (uint8_t*)VGA_FRAMBUFFER_WINDOW_START + current_video_mode_width * 2 * y + 2 * x;
    for (int xi = current_video_mode_width * 2; xi--;) {
        if (!*string) break;
        *t_buf++ = *string++;
        *t_buf++ = bgcolor << 4 | color & 0xF;
    }
}

void draw_debug_text(const char* string, uint32_t x, uint32_t y, uint8_t color, uint8_t bgcolor) {
    uint32_t w = SELECT_VGA ? (current_video_mode == 3 ? 80 : 40) : 53;
    uint8_t* t_buf = (uint8_t*)SCREEN + w * 2 * y + 2 * x;
    for (int xi = w * 2; xi--; ) {
        *t_buf++ = *string ? *string++ : ' ';
        *t_buf++ = bgcolor << 4 | color & 0xF;
    }
}

void draw_window(const char* title, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    char line[width + 1];
    memset(line, 0, sizeof line);
    width--;
    height--;
    // Рисуем рамки

    memset(line, 0xCD, width); // ═══


    line[0] = 0xC9; // ╔
    line[width] = 0xBB; // ╗
    draw_text(line, x, y, 11, 1);

    line[0] = 0xC8; // ╚
    line[width] = 0xBC; //  ╝
    draw_text(line, x, height + y, 11, 1);

    memset(line, ' ', width);
    line[0] = line[width] = 0xBA;

    for (int i = 1; i < height; i++) {
        draw_text(line, x, y + i, 11, 1);
    }

    snprintf(line, width - 1, " %s ", title);
    draw_text(line, x + (width - strlen(line)) / 2, y, 14, 3);
}

void clrScr(const uint8_t bgColor) {
    uint16_t* t_buf = (uint16_t*)VGA_FRAMBUFFER_WINDOW_START;
    t_buf += text_page * VGA_FRAMBUFFER_WINDOW_SIZE;
    int size = current_video_mode_width * current_video_mode_height;
    while (size--) *t_buf++ = (bgColor << 4) | ' ';
}

void clrBuf(void) {
    memset(SCREEN, 0, sizeof(SCREEN));
}

void goutf(int outline, bool err, const char *__restrict str, ...) {
    va_list ap;
    char buf[80];
    va_start(ap, str);
    vsnprintf(buf, 80, str, ap);
    va_end(ap);
    draw_debug_text(buf, 0, outline, err ? 12 : 7, 0);
}

void gprintf(int outline, uint8_t color, uint8_t bgColor, const char *__restrict str, ...) {
    va_list ap;
    char buf[80];
    va_start(ap, str);
    vsnprintf(buf, 80, str, ap);
    va_end(ap);
    draw_text(buf, 0, outline, color, bgColor);
}
