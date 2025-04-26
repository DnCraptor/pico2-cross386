#include "graphics.h"
#include <string.h>

bool SELECT_VGA = true;

uint8_t SCREEN[256l << 10]; // Emulate 256 KB of frame-buffer - real buffer
// in PSRAM:
uint32_t VGA_FRAMBUFFER_WINDOW_START = 0x11000000 + 0xB8000; // 0x03 video mode
uint32_t VGA_FRAMBUFFER_WINDOW_SIZE = 80 * 25 * 2; // 0x03 video mode
// 320x200 пикселей с 256 цветами (мод 0x13), 0xA0000, 320×200=64000 байт=0xF800 байт

void __time_critical_func() handle_frame_changed() {
    memcpy(SCREEN, (uint8_t*)VGA_FRAMBUFFER_WINDOW_START, VGA_FRAMBUFFER_WINDOW_SIZE);
}

int get_text_cols() {
    return SELECT_VGA ? 80 : 53;
}

void draw_text(const char* string, uint32_t x, uint32_t y, uint8_t color, uint8_t bgcolor) {
    uint8_t* t_buf = (uint8_t*)VGA_FRAMBUFFER_WINDOW_START + TEXTMODE_COLS * 2 * y + 2 * x;
    for (int xi = TEXTMODE_COLS * 2; xi--;) {
        if (!*string) break;
        *t_buf++ = *string++;
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

void clrScr(const uint8_t color) {
    uint16_t* t_buf = (uint16_t*)VGA_FRAMBUFFER_WINDOW_START;
    int size = TEXTMODE_COLS * TEXTMODE_ROWS;
    while (size--) *t_buf++ = color << 4 | ' ';
}
