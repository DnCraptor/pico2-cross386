#ifdef HDMIA

#include "graphics.h"

#include "common_dvi_pin_configs.h"
#include "dvi.h"
#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240

uint8_t* text_buffer = NULL;
struct dvi_inst dvi0;

void graphics_init() {
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	while (queue_is_empty(&dvi0.q_colour_valid))
		__wfe();
	dvi_start(&dvi0);
///	dvi_scanbuf_main_16bpp(&dvi0);
}

void graphics_set_bgcolor(const uint32_t color888) {

}

void graphics_set_offset(const int x, const int y) {

}

void graphics_set_flashmode(const bool flash_line, const bool flash_frame) {

}

void graphics_set_mode(enum graphics_mode_t mode) {

}

void graphics_set_buffer(uint8_t* buffer, const uint16_t width, const uint16_t height) {

}

void graphics_set_textbuffer(uint8_t* buffer) {
    text_buffer = buffer;
}

void clrScr(const uint8_t color) {
    uint16_t* t_buf = (uint16_t *)text_buffer;
    if (!t_buf) return;
    int size = TEXTMODE_COLS * TEXTMODE_ROWS;
    while (size--) *t_buf++ = color << 4 | ' ';
}

#endif // HDMIA
