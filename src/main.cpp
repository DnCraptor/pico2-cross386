#include <stdbool.h>
#include <cstdlib>
#include <cstring>
#include <math.h>
#include <pico.h>
#include <hardware/vreg.h>
#include <hardware/clocks.h>
#include <hardware/flash.h>
#include <hardware/watchdog.h>
#include <pico/bootrom.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <hardware/pwm.h>

#include "graphics.h"

#include "x86.h"

#ifdef ZERO
#undef PICO_DEFAULT_LED_PIN
// TODO:
#endif

#ifdef HDMIA
    #include "common_dvi_pin_configs.h"
    #include "dvi.h"
    extern "C" {
        #include "tmds_encode.h"
    }
    #define FRAME_WIDTH 320
    #define FRAME_HEIGHT 240
    #define DVI_TIMING dvi_timing_640x480p_60hz
    extern "C" struct dvi_inst dvi0;
    #define N_SCANLINE_BUFFERS 6
    uint16_t __attribute__((aligned(4))) static_scanbuf[N_SCANLINE_BUFFERS][FRAME_WIDTH];
#endif

extern "C" volatile int y = 0;

#include "psram_spi.h"
#include "nespad.h"
#include "ff.h"
#include "ps2kbd_mrmltr.h"

void print_psram_info();
void get_flash_info();
void get_sdcard_info();
void get_cpu_flash_jedec_id(uint8_t _rx[4]);

extern "C" void goutf(int outline, bool err, const char *__restrict str, ...) {
    va_list ap;
    char buf[80];
    va_start(ap, str);
    vsnprintf(buf, 80, str, ap);
    va_end(ap);
    draw_text(buf, 0, outline, err ? 12 : 7, 0);
}

struct input_bits_t {
    bool a: true;
    bool b: true;
    bool select: true;
    bool start: true;
    bool right: true;
    bool left: true;
    bool up: true;
    bool down: true;
};

input_bits_t gamepad1_bits = { false, false, false, false, false, false, false, false };

#if PICO_RP2040
volatile static bool no_butterbod = true;
#else
volatile static bool no_butterbod = false;
#endif
volatile static uint8_t pressed_key[256] = { 0 };
volatile static uint8_t mouse_buttons = 0;
volatile static bool i2s_1nit = false;
volatile static bool pwm_1nit = false;
volatile static uint32_t cpu = 0;
volatile static uint32_t vol = 0;
const int samples = 64;
static int16_t samplesL[samples][2];
static int16_t samplesR[samples][2];
static int16_t samplesLR[samples][2];

inline bool isSpeaker() {
    nespad_read();
    uint32_t nstate = nespad_state;
    uint32_t nstate2 = nespad_state2;
    return pressed_key[HID_KEY_S] || (nstate & DPAD_A) || (nstate2 & DPAD_A) || (mouse_buttons & MOUSE_BUTTON_MIDDLE) || gamepad1_bits.a;
}

inline bool isI2S() {
    uint32_t nstate = nespad_state;
    uint32_t nstate2 = nespad_state2;
    return pressed_key[HID_KEY_I] || (nstate & DPAD_B) || (nstate2 == DPAD_B) || gamepad1_bits.b;
}

inline bool isInterrupted() {
    return isSpeaker() || isI2S();
}

#if !PICO_RP2040
#include <hardware/structs/qmi.h>
#include <hardware/structs/xip.h>
volatile uint8_t * PSRAM_DATA = (uint8_t*)X86_RAM_BASE;
void __no_inline_not_in_flash_func(psram_init)(uint cs_pin) {
    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);

    // Enable direct mode, PSRAM CS, clkdiv of 10.
    qmi_hw->direct_csr = 10 << QMI_DIRECT_CSR_CLKDIV_LSB | \
                               QMI_DIRECT_CSR_EN_BITS | \
                               QMI_DIRECT_CSR_AUTO_CS1N_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS)
        ;

    // Enable QPI mode on the PSRAM
    const uint CMD_QPI_EN = 0x35;
    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | CMD_QPI_EN;

    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS)
        ;

    // Set PSRAM timing for APS6404
    //
    // Using an rxdelay equal to the divisor isn't enough when running the APS6404 close to 133MHz.
    // So: don't allow running at divisor 1 above 100MHz (because delay of 2 would be too late),
    // and add an extra 1 to the rxdelay if the divided clock is > 100MHz (i.e. sys clock > 200MHz).
    const int max_psram_freq = 166000000;
    const int clock_hz = clock_get_hz(clk_sys);
    int divisor = (clock_hz + max_psram_freq - 1) / max_psram_freq;
    if (divisor == 1 && clock_hz > 100000000) {
        divisor = 2;
    }
    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000) {
        rxdelay += 1;
    }

    // - Max select must be <= 8us.  The value is given in multiples of 64 system clocks.
    // - Min deselect must be >= 18ns.  The value is given in system clock cycles - ceil(divisor / 2).
    const int clock_period_fs = 1000000000000000ll / clock_hz;
    const int max_select = (125 * 1000000) / clock_period_fs;  // 125 = 8000ns / 64
    const int min_deselect = (18 * 1000000 + (clock_period_fs - 1)) / clock_period_fs - (divisor + 1) / 2;

    qmi_hw->m[1].timing = 1 << QMI_M1_TIMING_COOLDOWN_LSB |
                          QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
                          max_select << QMI_M1_TIMING_MAX_SELECT_LSB |
                          min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB |
                          rxdelay << QMI_M1_TIMING_RXDELAY_LSB |
                          divisor << QMI_M1_TIMING_CLKDIV_LSB;

    // Set PSRAM commands and formats
    qmi_hw->m[1].rfmt =
        QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB |\
        QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q   << QMI_M0_RFMT_ADDR_WIDTH_LSB |\
        QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB |\
        QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q  << QMI_M0_RFMT_DUMMY_WIDTH_LSB |\
        QMI_M0_RFMT_DATA_WIDTH_VALUE_Q   << QMI_M0_RFMT_DATA_WIDTH_LSB |\
        QMI_M0_RFMT_PREFIX_LEN_VALUE_8   << QMI_M0_RFMT_PREFIX_LEN_LSB |\
        6                                << QMI_M0_RFMT_DUMMY_LEN_LSB;

    qmi_hw->m[1].rcmd = 0xEB;

    qmi_hw->m[1].wfmt =
        QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB |\
        QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q   << QMI_M0_WFMT_ADDR_WIDTH_LSB |\
        QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB |\
        QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q  << QMI_M0_WFMT_DUMMY_WIDTH_LSB |\
        QMI_M0_WFMT_DATA_WIDTH_VALUE_Q   << QMI_M0_WFMT_DATA_WIDTH_LSB |\
        QMI_M0_WFMT_PREFIX_LEN_VALUE_8   << QMI_M0_WFMT_PREFIX_LEN_LSB;

    qmi_hw->m[1].wcmd = 0x38;

    // Disable direct mode
    qmi_hw->direct_csr = 0;

    // Enable writes to PSRAM
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);
}
#endif

extern "C" {
    #include "audio.h"
#if 0
    #include "ps2.h"
    bool __time_critical_func(handleScancode)(const uint32_t ps2scancode) {
        goutf(TEXTMODE_ROWS - 3, false, "Last scancode: %04Xh                                ", ps2scancode);
        if (ps2scancode == 0x1F) { // S
            Spressed = true;
        }
        else if (ps2scancode == 0x26) { // L
            Lpressed = true;
        }
        else if (ps2scancode == 0x13) { // R
            Rpressed = true;
        }
        else if (ps2scancode == 0x17) { // I
            Ipressed = true;
        }
        else if (ps2scancode == 0x9F) { // S up
            Spressed = false;
        }
        else if (ps2scancode == 0xA6) { // L up
            Lpressed = false;
        }
        else if (ps2scancode == 0x93) { // R up
            Rpressed = false;
        }
        else if (ps2scancode == 0x97) { // I up
            Ipressed = false;
        }
        else if (ps2scancode == 0x1D) {
            ctrlPressed = true;
        }
        else if (ps2scancode == 0x9D) {
            ctrlPressed = false;
        }
        else if (ps2scancode == 0x38) {
            altPressed = true;
        }
        else if (ps2scancode == 0xB8) {
            altPressed = false;
        }
#if SDCARD_INFO        
        else if (ps2scancode == 0x20) { // D is down (SD CARD info)
            clrScr(0);
            y = 0;
            get_sdcard_info();
            footer();
        }
#endif
        else if (ps2scancode == 0x21) { // F is down (Flash info)
            clrScr(0);
            y = 0;
            get_flash_info();
            footer();
        }
        else if (ps2scancode == 0x19) { // P is down (PSRAM info)
            clrScr(0);
            y = 0;
            print_psram_info();
            footer();
        }
        else if (ps2scancode == 0x53 && altPressed && ctrlPressed) {
        }
        else if (ps2scancode == 0x4E) { // +
        }
        else if (ps2scancode == 0x4A) { // -
        }
        else if (ps2scancode == 0x52) { // Ins
        }
        else if (ps2scancode == 0x53) { // Del
        }
        else if (ps2scancode == 0x49) { // PageUp
        }
        else if (ps2scancode == 0x51) { // PageDown
        }
        return true;
    }
#endif
}

void process_mouse_report(hid_mouse_report_t const * report)
{
    mouse_buttons = report->buttons;
    goutf(current_video_mode_height - 2, false, "Mouse X: %d Y: %d Wheel: %02Xh Buttons: %02Xh         ", report->x, report->y, report->wheel, report->buttons);
  /*
  //------------- button state  -------------//
  uint8_t button_changed_mask = report->buttons ^ prev_report.buttons;
  if ( button_changed_mask & report->buttons)
  {
    printf(" %c%c%c ",
       report->buttons & MOUSE_BUTTON_LEFT   ? 'L' : '-',
       report->buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-',
       report->buttons & MOUSE_BUTTON_RIGHT  ? 'R' : '-');
  }

  //------------- cursor movement -------------//
  cursor_movement(report->x, report->y, report->wheel);
  */
}

inline static bool isInReport(hid_keyboard_report_t const *report, const unsigned char keycode) {
    for (unsigned char i: report->keycode) {
        if (i == keycode) {
            return true;
        }
    }
    return false;
}

extern Ps2Kbd_Mrmltr ps2kbd;

void __not_in_flash_func(process_kbd_report)(
    hid_keyboard_report_t const *report,
    hid_keyboard_report_t const *prev_report
) {
    goutf(current_video_mode_height - 3, false, "HID modifiers: %02Xh                                ", report->modifier);
    pressed_key[HID_KEY_ALT_LEFT] = report->modifier & KEYBOARD_MODIFIER_LEFTALT;
    pressed_key[HID_KEY_ALT_RIGHT] = report->modifier & KEYBOARD_MODIFIER_RIGHTALT;
    pressed_key[HID_KEY_CONTROL_LEFT] = report->modifier & KEYBOARD_MODIFIER_LEFTCTRL;
    pressed_key[HID_KEY_CONTROL_RIGHT] = report->modifier & KEYBOARD_MODIFIER_RIGHTCTRL;
    for (uint8_t pkc: prev_report->keycode) {
        if (!pkc) continue;
        bool key_still_pressed = false;
        for (uint8_t kc: report->keycode) {
            if (kc == pkc) {
                key_still_pressed = true;
                break;
            }
        }
        if (!key_still_pressed) {
         ///   kbd_queue_push(pressed_key[pkc], false);
            pressed_key[pkc] = 0;
            goutf(current_video_mode_height - 3, false, "Release hid_code: %02Xh modifiers: %02Xh            ", pkc, report->modifier);
        }
    }
    for (uint8_t kc: report->keycode) {
        if (!kc) continue;
        volatile uint8_t* pk = pressed_key + kc;
        uint8_t hid_code = *pk;
        if (hid_code == 0) { // it was not yet pressed
            hid_code = kc;
            if (hid_code != 0) {
                *pk = hid_code;
            ///    kbd_queue_push(hid_code, true);
                goutf(current_video_mode_height - 3, false, "Hit hid_code: %02Xh modifiers: %02Xh             ", hid_code, report->modifier);
            }
        }
    }
    if (pressed_key[HID_KEY_CONTROL_LEFT] && pressed_key[HID_KEY_ALT_LEFT] && pressed_key[HID_KEY_DELETE]) {
        watchdog_enable(1, 0);
    }
    else if (pressed_key[HID_KEY_KEYPAD_ADD]) { // +
    }
    else if (pressed_key[HID_KEY_KEYPAD_SUBTRACT]) { // -
    }
    else if (pressed_key[HID_KEY_INSERT]) { // Ins
    }
    else if (pressed_key[HID_KEY_DELETE]) { // Del
    }
    else if (pressed_key[HID_KEY_PAGE_UP]) { // PageUp
    }
    else if (pressed_key[HID_KEY_PAGE_DOWN]) { // PageDown
    }

}

Ps2Kbd_Mrmltr ps2kbd(
    pio1,
    KBD_CLOCK_PIN,
    process_kbd_report
);

///static uint16_t dma_buffer[22050 / 60];
static i2s_config_t i2s_config = {
    .sample_freq = 22050,
    .channel_count = 2,
    .data_pin = AUDIO_DATA_PIN,
    .clock_pin_base = AUDIO_CLOCK_PIN,
    .pio = pio1,
    .sm = 0,
    .dma_channel = 0,
    .dma_trans_count = 0, ///sizeof(dma_buffer) / sizeof(uint32_t), // Number of 32 bits words to transfer
    .dma_buf = 0,
    .volume = 0, // 16 - is 0
};
    
static semaphore vga_start_semaphore;

#ifdef HDMIA
#define __dvi_func_x(f) __scratch_x(__STRING(f)) f
static inline void __dvi_func_x(_dvi_prepare_scanline_16bpp)(struct dvi_inst *inst, uint32_t *scanbuf) {
    uint32_t *tmdsbuf = NULL;
    queue_remove_blocking_u32(&inst->q_tmds_free, &tmdsbuf);
    uint pixwidth = inst->timing->h_active_pixels;
    uint words_per_channel = pixwidth / DVI_SYMBOLS_PER_WORD;
    tmds_encode_data_channel_16bpp(scanbuf, tmdsbuf + 0 * words_per_channel, pixwidth / 2, DVI_16BPP_BLUE_MSB,  DVI_16BPP_BLUE_LSB );
    tmds_encode_data_channel_16bpp(scanbuf, tmdsbuf + 1 * words_per_channel, pixwidth / 2, DVI_16BPP_GREEN_MSB, DVI_16BPP_GREEN_LSB);
    tmds_encode_data_channel_16bpp(scanbuf, tmdsbuf + 2 * words_per_channel, pixwidth / 2, DVI_16BPP_RED_MSB,   DVI_16BPP_RED_LSB  );
    queue_add_blocking_u32(&inst->q_tmds_valid, &tmdsbuf);
}
#endif

void __time_critical_func(render_core)() {
    multicore_lockout_victim_init();
    graphics_init();
    const auto buffer = (uint8_t *)SCREEN;
    graphics_set_bgcolor(0x000000);
    graphics_set_offset(0, 0);
    graphics_set_flashmode(false, false);
    graphics_set_mode(TEXTMODE_DEFAULT);
//    graphics_set_buffer(buffer, TEXTMODE_COLS, TEXTMODE_ROWS);
//    graphics_set_textbuffer(buffer);
    clrScr(0);
    sem_acquire_blocking(&vga_start_semaphore);
#ifdef HDMIA
while (1) {
    uint32_t *scanbuf = NULL;
    queue_remove_blocking_u32(&dvi0.q_colour_valid, &scanbuf);
    _dvi_prepare_scanline_16bpp(&dvi0, scanbuf);
    queue_add_blocking_u32(&dvi0.q_colour_free, &scanbuf);
}
#endif
    // 60 FPS loop
#define frame_tick (16666)
    uint64_t tick = time_us_64();
    uint64_t last_frame_tick = tick;
    static uint32_t frame_no = 0;
    while (true) {
        if (tick >= last_frame_tick + frame_tick) {
            frame_no++;
#ifdef TFT
            refresh_lcd();
#endif
            /**
            if (i2s_1nit && (Lpressed || Rpressed)) {
                uint16_t out = (frame_no & 1) ? 0xFFFF : 0;
                for (int i = 0; i < (sizeof(dma_buffer) / sizeof(uint16_t)); ) {
                    dma_buffer[i++] = Lpressed ? out : 0;
                    dma_buffer[i++] = Rpressed ? out : 0;
                }
                i2s_dma_write(&i2s_config, dma_buffer);
            }
            */
            last_frame_tick = tick;
            ps2kbd.tick();
        }
        tuh_task();
        tick = time_us_64();
    }
    __unreachable();
}

static bool __not_in_flash_func(write_flash)(void) {
    static uint8_t buffer[FLASH_SECTOR_SIZE];
    memcpy(buffer, (const void*)XIP_BASE, FLASH_SECTOR_SIZE);
    uint32_t flash_target_offset = 0x20000;

    multicore_lockout_start_blocking();
    const uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_target_offset, FLASH_SECTOR_SIZE);
    flash_range_program(flash_target_offset, buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();

    return memcmp((const void*)XIP_BASE, (const void*)XIP_BASE + flash_target_offset, FLASH_SECTOR_SIZE) == 0;
}

static pwm_config config = pwm_get_default_config();
static void PWM_init_pin(uint8_t pinN, uint16_t max_lvl) {
    gpio_set_function(pinN, GPIO_FUNC_PWM);
    pwm_config_set_clkdiv(&config, 1.0);
    pwm_config_set_wrap(&config, max_lvl); // MAX PWM value
    pwm_init(pwm_gpio_to_slice_num(pinN), &config, true);
}

static const char* get_volt() {
    const char* volt = (const char*)"1.3 V";
    switch(vol) {
#if !PICO_RP2040
        case VREG_VOLTAGE_0_60: volt = "0.6 V"; break;
        case VREG_VOLTAGE_0_65: volt = "0.65V"; break;
        case VREG_VOLTAGE_0_70: volt = "0.7 V"; break;
        case VREG_VOLTAGE_0_75: volt = "0.75V"; break;
        case VREG_VOLTAGE_0_80: volt = "0.8 V"; break;
#endif
        case VREG_VOLTAGE_0_85: volt = "0.85V"; break;
        case VREG_VOLTAGE_0_90: volt = "0.9 V"; break;
        case VREG_VOLTAGE_0_95: volt = "0.95V"; break;
        case VREG_VOLTAGE_1_00: volt = "1.0 V"; break;
        case VREG_VOLTAGE_1_05: volt = "1.05V"; break;
        case VREG_VOLTAGE_1_10: volt = "1.1 V"; break;
        case VREG_VOLTAGE_1_15: volt = "1.15V"; break;
        case VREG_VOLTAGE_1_20: volt = "1.2 V"; break;
        case VREG_VOLTAGE_1_25: volt = "1.25V"; break;
        case VREG_VOLTAGE_1_30: volt = "1.3 V"; break;
#if !PICO_RP2040
        // Above this point you will need to set POWMAN_VREG_CTRL_DISABLE_VOLTAGE_LIMIT
        case VREG_VOLTAGE_1_35: volt = "1.35V"; break;
        case VREG_VOLTAGE_1_40: volt = "1.4 V"; break;
        case VREG_VOLTAGE_1_50: volt = "1.5 V"; break;
        case VREG_VOLTAGE_1_60: volt = "1.6 V"; break;
        case VREG_VOLTAGE_1_65: volt = "1.65V"; break;
        case VREG_VOLTAGE_1_70: volt = "1.7 V"; break;
        case VREG_VOLTAGE_1_80: volt = "1.8 V"; break;
        case VREG_VOLTAGE_1_90: volt = "1.9 V"; break;
        case VREG_VOLTAGE_2_00: volt = "2.0 V"; break;
        case VREG_VOLTAGE_2_35: volt = "2.35V"; break;
        case VREG_VOLTAGE_2_50: volt = "2.5 V"; break;
        case VREG_VOLTAGE_2_65: volt = "2.65V"; break;
        case VREG_VOLTAGE_2_80: volt = "2.8 V"; break;
        case VREG_VOLTAGE_3_00: volt = "3.0 V"; break;
        case VREG_VOLTAGE_3_15: volt = "3.15V"; break;
        case VREG_VOLTAGE_3_30: volt = "3.3 V"; break;
#endif
    }
    return volt;
}

#include <hardware/exception.h>

void sigbus(void) {
    goutf(y++, true, "SIGBUS exception caught...");
    // reset_usb_boot(0, 0);
}
void __attribute__((naked, noreturn)) __printflike(1, 0) dummy_panic(__unused const char *fmt, ...) {
    goutf(y++, true, "*** PANIC ***");
    if (fmt)
        goutf(y++, true, fmt);
}

#ifdef HDMIA
#include "wikimedia_christmas_tree_in_field_320x240_rgb565.h"
void __scratch_x("render") render_scanline(uint16_t *scanbuf, uint raster_y) {
	// Use DMA to copy in background line (for speed)
	uint dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
    channel_config_set_write_increment(&cfg, true);
    dma_channel_configure(
    	dma_chan,
    	&cfg,
    	scanbuf,
    	&((const uint16_t*)wikimedia_christmas_tree_in_field_320x240)[raster_y * FRAME_WIDTH],
    	FRAME_WIDTH / 2,
    	true
    );
    dma_channel_wait_for_finish_blocking(dma_chan);
    dma_channel_unclaim(dma_chan);
}
#endif

#if !PICO_RP2040
#ifdef BUTTER_PSRAM_GPIO

#include <hardware/exception.h>
#include <hardware/structs/qmi.h>
#include <hardware/structs/xip.h>
#ifdef BUTTER_PSRAM_GPIO
#define MB16 (16ul << 20)
#define MB8 (8ul << 20)
#define MB4 (4ul << 20)
#define MB1 (1ul << 20)
uint32_t __not_in_flash_func(butter_psram_size)() {
    static int BUTTER_PSRAM_SIZE = -1;
    if (BUTTER_PSRAM_SIZE != -1) return BUTTER_PSRAM_SIZE;

    for(register int i = MB16 - MB1; i < MB16; ++i)
        PSRAM_DATA[i] = i & 0xFF;
    register uint32_t res = 0;
    for(register int i = MB4 - MB1; i < MB4; ++i) {
        if (PSRAM_DATA[i] != (i & 0xFF)) {
            for(register int i = MB8 - MB1; i < MB8; ++i) {
                if (PSRAM_DATA[i] != (i & 0xFF)) {
                    for(register int i = MB16 - MB1; i < MB16; ++i) {
                        if (PSRAM_DATA[i] != (i & 0xFF)) {
                            goto e0;
                        }
                    }
                    res = MB16;
                    goto e0;
                }
            }
            res = MB8;
            goto e0;
        }
    }
    res = MB4;
e0:
    BUTTER_PSRAM_SIZE = res;
    return res;
}
#else
static uint8_t* PSRAM_DATA = (uint8_t*)0;
#endif
#else
static uint8_t* PSRAM_DATA = (uint8_t*)0;
#endif
#endif

inline static u32 x86_int10(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    register u32 in_eax __asm__("r0") = eax;
    register u32 in_ebx __asm__("r1") = ebx;
    register u32 in_ecx __asm__("r2") = ecx;
    register u32 in_edx __asm__("r3") = edx;
    register u32 result __asm__("r0"); // результат будет в r0
    __asm__ volatile (
        "  push {r1-r12, lr}\n"         // Сохраняем рабочие регистры (r1-r12, lr)
        "  mov  r4, r0\n"               // r4 = EAX
        "  mov  r5, r1\n"               // r5 = EBX
        "  mov  r6, r2\n"               // r6 = ECX
        "  mov  r7, r3\n"               // r7 = EDX
        "  CPSID i\n"                   // Запрет прерываний
        "  adr  r11, 1f\n"              // Адрес возврата (метка 1)
        "  mrs  r12, apsr\n"            // Сохраняем флаги
        "  push {r11, r12}\n"           // Эмулируем PUSH IP, PUSH FLAGS
        "  ldr  r11, =0x11000040\n"     // Адрес обработчика INT 10h
        "  ldr  r11, [r11]\n"
        "  mov  pc, r11\n"              // Переход к обработчику
        "1:\n"                          // Метка возврата
        "  mov  r0, r4\n"               // В r0 результат (из r4)
        "  pop  {r1-r12, lr}\n"          // Восстанавливаем сохранённые регистры
        :
        : "r"(in_eax), "r"(in_ebx), "r"(in_ecx), "r"(in_edx)
        : "r4", "r5", "r6", "r7", "r11", "r12", "memory"
    );
    return result;
}

inline static u32 x86_int13(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    register u32 in_eax __asm__("r0") = eax;
    register u32 in_ebx __asm__("r1") = ebx;
    register u32 in_ecx __asm__("r2") = ecx;
    register u32 in_edx __asm__("r3") = edx;
    register u32 result __asm__("r0"); // результат будет в r0
    __asm__ volatile (
        "  push {r1-r12, lr}\n"         // Сохраняем рабочие регистры (r1-r12, lr)
        "  mov  r4, r0\n"               // r4 = EAX
        "  mov  r5, r1\n"               // r5 = EBX
        "  mov  r6, r2\n"               // r6 = ECX
        "  mov  r7, r3\n"               // r7 = EDX
        "  CPSID i\n"                   // Запрет прерываний
        "  adr  r11, 1f\n"              // Адрес возврата (метка 1)
        "  mrs  r12, apsr\n"            // Сохраняем флаги
        "  push {r11, r12}\n"           // Эмулируем PUSH IP, PUSH FLAGS
        "  ldr  r11, =0x1100004C\n"     // Адрес обработчика INT 13h
        "  ldr  r11, [r11]\n"
        "  mov  pc, r11\n"              // Переход к обработчику
        "1:\n"                          // Метка возврата
        "  mov  r0, r4\n"               // В r0 результат (из r4)
        "  pop  {r1-r12, lr}\n"          // Восстанавливаем сохранённые регистры
        :
        : "r"(in_eax), "r"(in_ebx), "r"(in_ecx), "r"(in_edx)
        : "r4", "r5", "r6", "r7", "r11", "r12", "memory"
    );
    return result;
}

static void format_hdd_test(int y) {
    u32 total_tracks = 512;          // Максимальное количество дорожек
    u32 heads = 256;                 // Максимальное количество головок
    u32 sectors_per_track = 63;      // Максимальное количество секторов на дорожку

    u8* buff = X86_FAR_PTR(X86_ES, 0x1000);  // Буфер для форматирования

    for (u32 track = 510; track < total_tracks; ++track) {
        for (u32 head = 0; head < heads; ++head) {
            // Заполняем буфер F/N парами
            for (u32 sector = 0; sector < sectors_per_track; ++sector) {
                buff[sector * 2 + 0] = 0x00;      // F = хороший сектор
                buff[sector * 2 + 1] = sector + 1; // N = номер сектора (начинается с 1)
            }

            // Формируем регистры для INT 13h AH=05h
            u32 ch = track & 0xFF;
            u32 cl = ((track >> 8) & 0x03) << 6;  // старшие 2 бита номера цилиндра
            cl |= 1;                              // номер первого сектора (обычно 1)

            u32 eax = (5 << 8) | sectors_per_track;    // AH=05h, AL=кол-во секторов
            u32 ebx = 0x1000;                         // смещение 0x1000 в сегменте ES
            u32 ecx = (ch << 8) | cl;                 // CH:CL
            u32 edx = (head << 8) | 0x80;             // DH:DL

            u32 status = x86_int13(eax, ebx, ecx, edx);

            goutf(y, false, "INT 13 AH=5 format [%d:%d:1-63] rc: %08X", track, head, status);
            if (status) return;
        }
    }
}

// connection is possible 00->00 (external pull down)
static int test_0000_case(uint32_t pin0, uint32_t pin1, int res) {
    gpio_init(pin0);
    gpio_set_dir(pin0, GPIO_OUT);
    sleep_ms(33);
    gpio_put(pin0, 1);

    gpio_init(pin1);
    gpio_set_dir(pin1, GPIO_IN);
    gpio_pull_down(pin1); /// external pulled down (so, just to ensure)
    sleep_ms(33);
    if ( gpio_get(pin1) ) { // 1 -> 1, looks really connected
        res |= (1 << 5) | 1;
    }
    gpio_deinit(pin0);
    gpio_deinit(pin1);
    return res;
}

// connection is possible 01->01 (no external pull up/down)
static int test_0101_case(uint32_t pin0, uint32_t pin1, int res) {
    gpio_init(pin0);
    gpio_set_dir(pin0, GPIO_OUT);
    sleep_ms(33);
    gpio_put(pin0, 1);

    gpio_init(pin1);
    gpio_set_dir(pin1, GPIO_IN);
    gpio_pull_down(pin1);
    sleep_ms(33);
    if ( gpio_get(pin1) ) { // 1 -> 1, looks really connected
        res |= (1 << 5) | 1;
    }
    gpio_deinit(pin0);
    gpio_deinit(pin1);
    return res;
}

// connection is possible 11->11 (externally pulled up)
static int test_1111_case(uint32_t pin0, uint32_t pin1, int res) {
    gpio_init(pin0);
    gpio_set_dir(pin0, GPIO_OUT);
    sleep_ms(33);
    gpio_put(pin0, 0);

    gpio_init(pin1);
    gpio_set_dir(pin1, GPIO_IN);
    gpio_pull_up(pin1); /// external pulled up (so, just to ensure)
    sleep_ms(33);
    if ( !gpio_get(pin1) ) { // 0 -> 0, looks really connected
        res |= 1;
    }
    gpio_deinit(pin0);
    gpio_deinit(pin1);
    return res;
}

static int testPins(uint32_t pin0, uint32_t pin1) {
    int res = 0b000000;
    /// do not try to test butter psram this way
#ifdef BUTTER_PSRAM_GPIO
    if (pin0 == BUTTER_PSRAM_GPIO || pin1 == BUTTER_PSRAM_GPIO) return res;
#endif
    if (pin0 == PICO_DEFAULT_LED_PIN || pin1 == PICO_DEFAULT_LED_PIN) return res; // LED
    if (pin0 == 23 || pin1 == 23) return res; // SMPS Power
    if (pin0 == 24 || pin1 == 24) return res; // VBus sense
    // try pull down case (passive)
    gpio_init(pin0);
    gpio_set_dir(pin0, GPIO_IN);
    gpio_pull_down(pin0);
    gpio_init(pin1);
    gpio_set_dir(pin1, GPIO_IN);
    gpio_pull_down(pin1);
    sleep_ms(33);
    int pin0vPD = gpio_get(pin0);
    int pin1vPD = gpio_get(pin1);
    gpio_deinit(pin0);
    gpio_deinit(pin1);
    /// try pull up case (passive)
    gpio_init(pin0);
    gpio_set_dir(pin0, GPIO_IN);
    gpio_pull_up(pin0);
    gpio_init(pin1);
    gpio_set_dir(pin1, GPIO_IN);
    gpio_pull_up(pin1);
    sleep_ms(33);
    int pin0vPU = gpio_get(pin0);
    int pin1vPU = gpio_get(pin1);
    gpio_deinit(pin0);
    gpio_deinit(pin1);

    res = (pin0vPD << 4) | (pin0vPU << 3) | (pin1vPD << 2) | (pin1vPU << 1);

    if (pin0vPD == 1) {
        if (pin0vPU == 1) { // pin0vPD == 1 && pin0vPU == 1
            if (pin1vPD == 1) { // pin0vPD == 1 && pin0vPU == 1 && pin1vPD == 1
                if (pin1vPU == 1) { // pin0vPD == 1 && pin0vPU == 1 && pin1vPD == 1 && pin1vPU == 1
                    // connection is possible 11->11 (externally pulled up)
                    return test_1111_case(pin0, pin1, res);
                } else { // pin0vPD == 1 && pin0vPU == 1 && pin1vPD == 1 && pin1vPU == 0
                    // connection is impossible
                    return res;
                }
            } else { // pin0vPD == 1 && pin0vPU == 1 && pin1vPD == 0
                if (pin1vPU == 1) { // pin0vPD == 1 && pin0vPU == 1 && pin1vPD == 0 && pin1vPU == 1
                    // connection is impossible
                    return res;
                } else { // pin0vPD == 1 && pin0vPU == 1 && pin1vPD == 0 && pin1vPU == 0
                    // connection is impossible
                    return res;
                }
            }
        } else {  // pin0vPD == 1 && pin0vPU == 0
            if (pin1vPD == 1) { // pin0vPD == 1 && pin0vPU == 0 && pin1vPD == 1
                if (pin1vPU == 1) { // pin0vPD == 1 && pin0vPU == 0 && pin1vPD == 1 && pin1vPU == 1
                    // connection is impossible
                    return res;
                } else { // pin0vPD == 1 && pin0vPU == 0 && pin1vPD == 1 && pin1vPU == 0
                    // connection is possible 10->10 (pulled up on down, and pulled down on up?)
                    return res |= (1 << 5) | 1; /// NOT SURE IT IS POSSIBLE TO TEST SUCH CASE (TODO: think about real cases)
                }
            } else { // pin0vPD == 1 && pin0vPU == 0 && pin1vPD == 0
                if (pin1vPU == 1) { // pin0vPD == 1 && pin0vPU == 0 && pin1vPD == 0 && pin1vPU == 1
                    // connection is impossible
                    return res;
                } else { // pin0vPD == 1 && pin0vPU == 0 && pin1vPD == 0 && pin1vPU == 0
                    // connection is impossible
                    return res;
                }
            }
        }
    } else { // pin0vPD == 0
        if (pin0vPU == 1) { // pin0vPD == 0 && pin0vPU == 1
            if (pin1vPD == 1) { // pin0vPD == 0 && pin0vPU == 1 && pin1vPD == 1
                if (pin1vPU == 1) { // pin0vPD == 0 && pin0vPU == 1 && pin1vPD == 1 && pin1vPU == 1
                    // connection is impossible
                    return res;
                } else { // pin0vPD == 0 && pin0vPU == 1 && pin1vPD == 1 && pin1vPU == 0
                    // connection is impossible
                    return res;
                }
            } else { // pin0vPD == 0 && pin0vPU == 1 && pin1vPD == 0
                if (pin1vPU == 1) { // pin0vPD == 0 && pin0vPU == 1 && pin1vPD == 0 && pin1vPU == 1
                    // connection is possible 01->01 (no external pull up/down)
                    return test_0101_case(pin0, pin1, res);
                } else { // pin0vPD == 0 && pin0vPU == 1 && pin1vPD == 0 && pin1vPU == 0
                    // connection is impossible
                    return res;
                }
            }
        } else {  // pin0vPD == 0 && pin0vPU == 0
            if (pin1vPD == 1) { // pin0vPD == 0 && pin0vPU == 0 && pin1vPD == 1
                if (pin1vPU == 1) { // pin0vPD == 0 && pin0vPU == 0 && pin1vPD == 1 && pin1vPU == 1
                    // connection is impossible
                    return res;
                } else { // pin0vPD == 0 && pin0vPU == 0 && pin1vPD == 1 && pin1vPU == 0
                    // connection is impossible
                    return res;
                }
            } else { // pin0vPD == 0 && pin0vPU == 0 && pin1vPD == 0
                if (pin1vPU == 1) { // pin0vPD == 0 && pin0vPU == 0 && pin1vPD == 0 && pin1vPU == 1
                    // connection is impossible
                    return res;
                } else { // pin0vPD == 0 && pin0vPU == 0 && pin1vPD == 0 && pin1vPU == 0
                    // connection is possible 00->00 (externally pulled down)
                    return test_0000_case(pin0, pin1, res);
                }
            }
        }
    }
    return res;
}

int main() {
#if !PICO_RP2040
    volatile uint32_t *qmi_m0_timing=(uint32_t *)0x400d000c;
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_60);
    sleep_ms(33);
    *qmi_m0_timing = 0x60007204;
    set_sys_clock_khz(378 * KHZ, 0);
    *qmi_m0_timing = 0x60007303;
#else
    hw_set_bits(&vreg_and_chip_reset_hw->vreg, VREG_AND_CHIP_RESET_VREG_VSEL_BITS);
    sleep_ms(10);
    set_sys_clock_khz(CPU_MHZ * KHZ, true);
#endif

#if PICO_RP2350
#ifdef BUTTER_PSRAM_GPIO
    psram_init(BUTTER_PSRAM_GPIO);
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, sigbus);
#endif
#endif

    int links = testPins(VGA_BASE_PIN, VGA_BASE_PIN + 1);
    SELECT_VGA = (links == 0) || (links == 0x1F);
    if (SELECT_VGA) {
        current_video_mode = 3;
        current_video_mode_width = 80;
        current_video_mode_height = 25;
    } else {
        current_video_mode = 0;
        current_video_mode_width = 80;
        current_video_mode_height = 25;
    }
    VGA_FRAMBUFFER_WINDOW_SIZE = current_video_mode_width * current_video_mode_height * 2;
    
    sem_init(&vga_start_semaphore, 0, 1);
    multicore_launch_core1(render_core);
    sem_release(&vga_start_semaphore);

#ifdef HDMIA
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

	for (int i = 0; i < N_SCANLINE_BUFFERS; ++i) {
		void *bufptr = &static_scanbuf[i];
		queue_add_blocking(&dvi0.q_colour_free, &bufptr);
	}

    uint frame_ctr = 0;
	uint16_t *scanbuf = 0;
	while (true) {
		for (uint y = 0; y < FRAME_HEIGHT; ++y) {
			queue_remove_blocking_u32(&dvi0.q_colour_free, &scanbuf);
			render_scanline(scanbuf, y);
			queue_add_blocking_u32(&dvi0.q_colour_valid, &scanbuf);
		}
		++frame_ctr;
	}
#endif

    FATFS fs;
///    draw_text("Init SDCARD", 0, TEXTMODE_ROWS - 1, 7, 0);
    if (f_mount(&fs, "SD", 1) == FR_OK) {
//        goutf(y++, false, "SDCARD %d FATs; %d free clusters (%d KB each)", fs.n_fats, f_getfree32(&fs), fs.csize >> 1);
        cd_card_mount = true;
        f_mkdir(HOME_DIR);
    } else {
        draw_text("SDCARD not connected", 0, y++, 12, 0);
    }

///    draw_text("Init keyboard", 0, TEXTMODE_ROWS - 1, 7, 0);
    tuh_init(BOARD_TUH_RHPORT);
    ps2kbd.init_gpio();
//    keyboard_init();
    sleep_ms(50);

#if PICO_RP2350
    #ifdef BUTTER_PSRAM_GPIO
    uint32_t psram32 = butter_psram_size();
    no_butterbod = psram32 == 0;
    goutf(y++, true, "Murmulator VGA/HDMI BIOS for RP2350 378 MHz 1.6V");
    goutf(y++, false, "PSRAM (on GPIO-%d) %d MB", BUTTER_PSRAM_GPIO, psram32 >> 20);
    if (0){
        uint32_t a = 0;
        uint32_t elapsed;
        uint32_t begin = time_us_32();
        double d = 1.0;
        double speedw, speedr;
        for (; a < psram32; ++a) {
            PSRAM_DATA[a] =  a & 0xFF;
        }
        elapsed = time_us_32() - begin;
        speedw = d * a / elapsed;
        goutf(y++, false, " 8-bit line write speed: %f MBps", speedw);
        begin = time_us_32();
        for (a = 0; a < psram32; ++a) {
            if ((a & 0xFF) != PSRAM_DATA[a]) {
                goutf(y++, false, " PSRAM read failed at %ph", PSRAM_DATA+a);
                break;
            }
        }
        elapsed = time_us_32() - begin;
        speedr = d * a / elapsed;
        goutf(y++, false, " 8-bit line read speed: %f MBps", speedr);
    }
    {
        uint32_t a = 0;
        uint32_t elapsed;
        uint32_t begin = time_us_32();
        double d = 1.0;
        double speedw, speedr;
        uint32_t* p32 = (uint32_t*)PSRAM_DATA;
        for (; a < psram32 / sizeof(uint32_t); ++a) {
            p32[a] = a;
        }
        elapsed = time_us_32() - begin;
        speedw = d * a * sizeof(uint32_t) / elapsed;
        goutf(y++, false, "32-bit line write speed: %f MBps", speedw);
        begin = time_us_32();
        for (a = 0; a < psram32 / sizeof(uint32_t); ++a) {
            if (a  != p32[a]) {
                goutf(y++, false, " PSRAM 32-bit read failed at %ph", p32+a);
                break;
            }
        }
        elapsed = time_us_32() - begin;
        speedr = d * a * sizeof(uint32_t) / elapsed;
        goutf(y++, false, "32-bit line read speed: %f MBps", speedr);
    }

    #endif
#endif
///    draw_text("Init NESPAD  ", 0, TEXTMODE_ROWS - 1, 7, 0);
    nespad_begin(clock_get_hz(clk_sys) / 1000, NES_GPIO_CLK, NES_GPIO_DATA, NES_GPIO_LAT);
    sleep_ms(50);

#if 0
    if (!isInterrupted()) {
//        draw_text("Test FLASH   ", 0, TEXTMODE_ROWS - 1, 7, 0);
        uint8_t rx[4];
        get_cpu_flash_jedec_id(rx);
        uint32_t flash_size = (1 << rx[3]);
        goutf(y++, false, "FLASH %d MB; JEDEC ID: %02X-%02X-%02X-%02X",
                 flash_size >> 20, rx[0], rx[1], rx[2], rx[3]
        );
    }
#endif
skip_it:
#if 0
    draw_text("         Red on White        ", 0, y++, 12, 15);
    draw_text("        Blue on Green        ", 0, y++, 1, 2);
    draw_text("       Marin on Red          ", 0, y++, 3, 4);
    draw_text("     Magenta on Yellow       ", 0, y++, 5, 6);
//    draw_text("        Gray on Black        ", 0, y++, 7, 8);
    draw_text("        Blue on LightGreen   ", 0, y++, 9, 10);
    draw_text("      Yellow on LightBlue    ", 0, y++, 6, 11);
    draw_text("       White on LightMagenta ", 0, y++, 15, 13);
    draw_text(" LightYellow on Gray         ", 0, y++, 14, 7);

    ///cli                 ; отключаем прерывания
    __asm volatile ("cpsid i" : : : "memory"); // __disable_irq();
    ///sti                 ; снова включаем прерывания
    __asm volatile ("cpsie i" : : : "memory"); // __enable_irq();
    /// cld/std - запомнить флаг направления MOVS и подобных операций

    /// xor ax, ax
    __asm volatile ("eor r0, r0, r0" : : : "memory");
    /// mov     ds, ax - сегметы как будем кодировать?
    /// mov     bp, BASE (0x7c00)
#endif

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    for (int i = 0; i < 6; i++) {
        sleep_ms(23);
        gpio_put(PICO_DEFAULT_LED_PIN, true);
        sleep_ms(23);
        gpio_put(PICO_DEFAULT_LED_PIN, false);
    }

    // init interrupts
    volatile uint32_t* X86_BASE_RAM = (uint32_t*)PSRAM_DATA;
    for (int i = 0; i < 256; ++i) {
        X86_BASE_RAM[i] = (uint32_t)&x86_iret;
    }
    X86_BASE_RAM[0x10] = (uint32_t)&x86_int10_hanler;
    X86_BASE_RAM[0x13] = (uint32_t)&x86_int13_hanler;
    u32 eax = x86_int10(0, 0, 0, 0);
    goutf(y++, false, "INT 10 AH=0 rc: %08X", eax);
#if 0
    u32 eax = x86_int13(0, 0, 0, 0);
    goutf(y++, false, "INT 13 AH=0 rc: %08X", eax);
    eax = x86_int13(1 << 8, 0, 0, 0);
    goutf(y++, false, "INT 13 AH=1 rc: %08X", eax);

    u8* buff = X86_FAR_PTR(X86_ES, 0x1000);
    for (int i = 0; i < 512; ++i)
        buff[i] = (u8)i;

    eax = x86_int13((3 << 8) | 1, 0x1000, 1, 0);
    goutf(y++, false, "INT 13 AH=3 AL=1 CL=1 ES:BX=[%04X:1000] rc: %08X", X86_ES, eax);
    
    eax = x86_int13((2 << 8) | 1, 0x1000, 1, 0);
    goutf(y++, false, "INT 13 AH=2 AL=1 CL=1 ES:BX=[%04X:1000] rc: %08X", X86_ES, eax);

    for (u32 track = 0; track < 80; ++track) {
        for (u32 head = 0; head < 2; ++head) {
            for (u32 sector = 0; sector < 18; ++sector) {
                buff[sector * 4 + 0] = track;         // Track number
                buff[sector * 4 + 1] = head;          // Head number
                buff[sector * 4 + 2] = sector + 1;    // Sector number (начинается с 1)
                buff[sector * 4 + 3] = 2;             // 512 bytes per sector
            }
            u32 eax = (5 << 8) | 18;   // AH=05h, AL=кол-во секторов
            u32 ebx = 0x1000;          // смещение 0x1000 в сегменте ES
            u32 ecx = (track << 8);    // CH=track, CL=0 пока
            u32 edx = (head << 8);     // DH=head, DL=0 (диск 0)
            u32 status = x86_int13(eax, ebx, ecx, edx);
            goutf(y, false, "INT 13 AH=5 format [%d:%d:1-18] rc: %08X", track, head, status);
        }
    }
    y++;
    format_hdd_test(y++);
#endif
    /*
    uint32_t entry_iret_official = (uint32_t)PSRAM_DATA + 0xff53;
    {
        uint16_t* code = reinterpret_cast<uint16_t*>(entry_iret_official);        
        code[0] = 0x4800;  // LDR r0, [PC, #0] (но PC указывает на addr+4), r0=iret_ptr
        code[1] = 0x4700;  // BX r0
        uint32_t* addr = reinterpret_cast<uint32_t*>(entry_iret_official + 4);
        *addr = reinterpret_cast<uint32_t>(iret_ptr);
    }
    */

///    uint8_t ov = *(uint8_t*)&gamepad1_bits;
    while(true) {
#if 0
        #if SDCARD_INFO        
        if (pressed_key[HID_KEY_D]) { // D is down (SD CARD info)
            clrScr(0);
            y = 0;
            get_sdcard_info();
            footer();
        } else
        #endif
        if (pressed_key[HID_KEY_F]) { // F is down (Flash info)
            clrScr(0);
            y = 0;
            get_flash_info();
            footer();
        }
        else if (pressed_key[HID_KEY_P]) { // P is down (PSRAM info)
            clrScr(0);
            y = 0;
            print_psram_info();
            footer();
        }
        uint32_t nstate = nespad_state;
        uint32_t nstate2 = nespad_state2;
        bool S = isSpeaker();
        bool I = isI2S();
        bool L = pressed_key[HID_KEY_L] || (nstate & DPAD_SELECT) || (nstate2 & DPAD_SELECT) || (mouse_buttons & MOUSE_BUTTON_LEFT ) || gamepad1_bits.select;
        bool R = pressed_key[HID_KEY_R] || (nstate &  DPAD_START) || (nstate2 &  DPAD_START) || (mouse_buttons & MOUSE_BUTTON_RIGHT) || gamepad1_bits.start;
        if (!i2s_1nit && (S || R || L)) {
            if (!pwm_1nit) {
                PWM_init_pin(BEEPER_PIN, (1 << 12) - 1);
                PWM_init_pin(PWM_PIN0  , (1 << 12) - 1);
                PWM_init_pin(PWM_PIN1  , (1 << 12) - 1);
                pwm_1nit = true;
                footer();
            }
            if (S) pwm_set_gpio_level(BEEPER_PIN, (1 << 12) - 1);
            if (R) pwm_set_gpio_level(PWM_PIN0  , (1 << 12) - 1);
            if (L) pwm_set_gpio_level(PWM_PIN1  , (1 << 12) - 1);
            sleep_ms(1);
            if (S) pwm_set_gpio_level(BEEPER_PIN, 0);
            if (R) pwm_set_gpio_level(PWM_PIN0  , 0);
            if (L) pwm_set_gpio_level(PWM_PIN1  , 0);
            sleep_ms(1);
        }
        else if (!pwm_1nit && I) {
            if (!i2s_1nit) {
                i2s_config.dma_trans_count = samples >> 1;
                i2s_init(&i2s_config);
                for (int i = 0; i < samples; ++i) {
                    int16_t v = std::sin(2 * 3.1415296 * i / samples) * 32767;
            
                    samplesL[i][0] = v;
                    samplesL[i][1] = 0;
            
                    samplesR[i][0] = 0;
                    samplesR[i][1] = v;
            
                    samplesLR[i][0] = v;
                    samplesLR[i][1] = v;
                }
                i2s_1nit = true;
                footer();
            }
        }
        else if (i2s_1nit && (L || R)) {
            i2s_dma_write(
                &i2s_config,
                (int16_t*)(L && R ? samplesLR : (L ? samplesL : samplesR))
            );
        }
        else {
            sleep_ms(100);
        }
        if (nstate != nespad_state || nstate2 != nespad_state2) {
            goutf(TEXTMODE_ROWS - 2, false, "NES PAD: %04Xh %04Xh                                ", nespad_state, nespad_state2);
        }
        uint8_t nv = *(uint8_t*)&gamepad1_bits;
        if (nv != ov) {
            goutf(TEXTMODE_ROWS - 2, false, "USB PAD: %02Xh                                      ", nv);
        }
#endif
    }
    __unreachable();
}
