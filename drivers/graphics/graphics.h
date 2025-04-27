#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "stdbool.h"
#include "stdio.h"
#include "stdint.h"

#ifdef TFT
#include "st7789.h"
#endif
#ifdef HDMI
#include "hdmi.h"
#endif
#ifdef VGA
#include "vga.h"
#endif
#ifdef TV
#include "tv.h"
#endif
#ifdef SOFTTV
#include "tv-software.h"
#endif

#include "font6x8.h"
#include "font8x8.h"
#include "font8x16.h"

enum graphics_mode_t {
    TEXTMODE_DEFAULT,
    GRAPHICSMODE_DEFAULT,

    TEXTMODE_53x30,

    TEXTMODE_160x100,

    CGA_160x200x16,
    CGA_320x200x4,
    CGA_640x200x2,

    TGA_320x200x16,
    EGA_320x200x16x4,
    VGA_320x240x256,
    VGA_320x200x256x4,
    // planar VGA
};

// Буффер текстового режима
extern uint8_t* text_buffer;

void graphics_init_hdmi();
void graphics_init();

void graphics_set_mode_hdmi(enum graphics_mode_t mode);
void graphics_set_mode(enum graphics_mode_t mode);

void graphics_set_buffer_hdmi(uint8_t* buffer, uint16_t width, uint16_t height);
void graphics_set_buffer(uint8_t* buffer, uint16_t width, uint16_t height);

void graphics_set_offset_hdmi(int x, int y);
void graphics_set_offset(int x, int y);

void graphics_set_palette_hdmi(uint8_t i, uint32_t color);
void graphics_set_palette(uint8_t i, uint32_t color);

void graphics_set_textbuffer(uint8_t* buffer);

void graphics_set_bgcolor_hdmi(uint32_t color888);
void graphics_set_bgcolor(uint32_t color888);

void graphics_set_flashmode(bool flash_line, bool flash_frame);

void draw_text(const char* string, uint32_t x, uint32_t y, uint8_t color, uint8_t bgcolor);
void draw_window(const char* title, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

void clrScr(uint8_t color);
void clrBuf(void);

extern bool SELECT_VGA;
extern uint32_t current_video_mode;
extern uint32_t text_cursor_type;
extern uint8_t text_cursor_row;
extern uint8_t text_cursor_column;
extern uint8_t text_page;

extern uint32_t current_video_mode_width;
extern uint32_t current_video_mode_height;
void handle_frame_changed();
extern uint8_t SCREEN[256l << 10]; // Emulate 256 KB of frame-buffer
// in PSRAM:
extern uint32_t VGA_FRAMBUFFER_WINDOW_START;
extern uint32_t VGA_FRAMBUFFER_WINDOW_SIZE;

/// to show data in debug ares
void goutf(int outline, bool err, const char *__restrict str, ...);
void draw_debug_text(const char* string, uint32_t x, uint32_t y, uint8_t color, uint8_t bgcolor);

#ifdef __cplusplus
}
#endif
