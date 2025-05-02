#include <stdbool.h>
#include <cstdlib>
#include <cstring>
#include <math.h>
#include <pico.h>
#include <hardware/vreg.h>
#include <hardware/clocks.h>
#include <hardware/flash.h>
#include <hardware/watchdog.h>
#include <hardware/structs/qmi.h>
#include <hardware/structs/xip.h>
#include <hardware/exception.h>
#include <hardware/structs/qmi.h>
#include <hardware/structs/xip.h>
#include <pico/bootrom.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <hardware/pwm.h>

#include "graphics.h"

#include "x86.h"

void get_cpu_flash_jedec_id(uint8_t _rx[4]);

#include "psram_spi.h"
#include "nespad.h"
#include "ff.h"
#include "ps2kbd_mrmltr.h"

void print_psram_info();
void get_flash_info();
void get_sdcard_info();
void get_cpu_flash_jedec_id(uint8_t _rx[4]);

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
///    goutf(current_video_mode_height - 2, false, "Mouse X: %d Y: %d Wheel: %02Xh Buttons: %02Xh         ", report->x, report->y, report->wheel, report->buttons);
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

extern Ps2Kbd_Mrmltr ps2kbd;

inline static 
uint8_t convert_keycode_to_scan(uint8_t keycode) {
    switch (keycode) {
        // Алфавитные клавиши
        case HID_KEY_A: return 0x1E;
        case HID_KEY_B: return 0x30;
        case HID_KEY_C: return 0x2E;
        case HID_KEY_D: return 0x20;
        case HID_KEY_E: return 0x12;
        case HID_KEY_F: return 0x21;
        case HID_KEY_G: return 0x22;
        case HID_KEY_H: return 0x23;
        case HID_KEY_I: return 0x17;
        case HID_KEY_J: return 0x24;
        case HID_KEY_K: return 0x25;
        case HID_KEY_L: return 0x26;
        case HID_KEY_M: return 0x32;
        case HID_KEY_N: return 0x31;
        case HID_KEY_O: return 0x18;
        case HID_KEY_P: return 0x19;
        case HID_KEY_Q: return 0x10;
        case HID_KEY_R: return 0x13;
        case HID_KEY_S: return 0x1F;
        case HID_KEY_T: return 0x14;
        case HID_KEY_U: return 0x16;
        case HID_KEY_V: return 0x2F;
        case HID_KEY_W: return 0x11;
        case HID_KEY_X: return 0x2D;
        case HID_KEY_Y: return 0x15;
        case HID_KEY_Z: return 0x2C;
        
        // Цифры
        case HID_KEY_1: return 0x02;
        case HID_KEY_2: return 0x03;
        case HID_KEY_3: return 0x04;
        case HID_KEY_4: return 0x05;
        case HID_KEY_5: return 0x06;
        case HID_KEY_6: return 0x07;
        case HID_KEY_7: return 0x08;
        case HID_KEY_8: return 0x09;
        case HID_KEY_9: return 0x0A;
        case HID_KEY_0: return 0x0B;
        
        // Специальные клавиши
        case HID_KEY_ENTER: return 0x1C;
        case HID_KEY_BACKSPACE: return 0x0E;
        case HID_KEY_TAB: return 0x0F;
        case HID_KEY_SPACE: return 0x39;
        case HID_KEY_MINUS: return 0x0C;
        case HID_KEY_EQUAL: return 0x0D;
        case HID_KEY_BRACKET_LEFT: return 0x1A;
        case HID_KEY_BRACKET_RIGHT: return 0x1B;
        case HID_KEY_BACKSLASH: return 0x2B;
        case HID_KEY_SEMICOLON: return 0x27;
        case HID_KEY_APOSTROPHE: return 0x28;
        case HID_KEY_GRAVE: return 0x29;
        case HID_KEY_COMMA: return 0x33;
        case HID_KEY_PERIOD: return 0x34;
        case HID_KEY_SLASH: return 0x35;
        
        // Клавиши управления
        case HID_KEY_CAPS_LOCK: return 0x3A;
        case HID_KEY_F1: return 0x3B;
        case HID_KEY_F2: return 0x3C;
        case HID_KEY_F3: return 0x3D;
        case HID_KEY_F4: return 0x3E;
        case HID_KEY_F5: return 0x3F;
        case HID_KEY_F6: return 0x40;
        case HID_KEY_F7: return 0x41;
        case HID_KEY_F8: return 0x42;
        case HID_KEY_F9: return 0x43;
        case HID_KEY_F10: return 0x44;
        case HID_KEY_F11: return 0x45;
        case HID_KEY_F12: return 0x46;
        case HID_KEY_PRINT_SCREEN: return 0x54;
        case HID_KEY_SCROLL_LOCK: return 0x47;
        case HID_KEY_PAUSE: return 0x48;
        
        // Стрелочные клавиши
        case HID_KEY_ARROW_UP: return 0x52;
        case HID_KEY_ARROW_DOWN: return 0x51;
        case HID_KEY_ARROW_LEFT: return 0x50;
        case HID_KEY_ARROW_RIGHT: return 0x4F;
        
        // Клавиши для работы с окнами
        case HID_KEY_INSERT: return 0x49;
        case HID_KEY_HOME: return 0x4A;
        case HID_KEY_PAGE_UP: return 0x4B;
        case HID_KEY_DELETE: return 0x4C;
        case HID_KEY_END: return 0x4D;
        case HID_KEY_PAGE_DOWN: return 0x4E;
        
        // Клавиши на цифровой панели
        case HID_KEY_KEYPAD_1: return 0x59;
        case HID_KEY_KEYPAD_2: return 0x5A;
        case HID_KEY_KEYPAD_3: return 0x5B;
        case HID_KEY_KEYPAD_4: return 0x5C;
        case HID_KEY_KEYPAD_5: return 0x5D;
        case HID_KEY_KEYPAD_6: return 0x5E;
        case HID_KEY_KEYPAD_7: return 0x5F;
        case HID_KEY_KEYPAD_8: return 0x60;
        case HID_KEY_KEYPAD_9: return 0x61;
        case HID_KEY_KEYPAD_0: return 0x62;
        case HID_KEY_KEYPAD_DECIMAL: return 0x63;
        case HID_KEY_KEYPAD_ENTER: return 0x58;
        
        // Дополнительные клавиши
        case HID_KEY_APPLICATION: return 0x65;
        case HID_KEY_POWER: return 0x66;
        case HID_KEY_KEYPAD_EQUAL: return 0x67;
        case HID_KEY_F13: return 0x68;
        case HID_KEY_F14: return 0x69;
        case HID_KEY_F15: return 0x6A;
        case HID_KEY_F16: return 0x6B;
        case HID_KEY_F17: return 0x6C;
        case HID_KEY_F18: return 0x6D;
        case HID_KEY_F19: return 0x6E;
        case HID_KEY_F20: return 0x6F;
        case HID_KEY_F21: return 0x70;
        case HID_KEY_F22: return 0x71;
        case HID_KEY_F23: return 0x72;
        case HID_KEY_F24: return 0x73;
        
        // Резерв
        case HID_KEY_CONTROL_LEFT: return 0xE0;
        case HID_KEY_SHIFT_LEFT: return 0xE1;
        case HID_KEY_ALT_LEFT: return 0xE2;
        case HID_KEY_GUI_LEFT: return 0xE3;
        case HID_KEY_CONTROL_RIGHT: return 0xE4;
        case HID_KEY_SHIFT_RIGHT: return 0xE5;
        case HID_KEY_ALT_RIGHT: return 0xE6;
        case HID_KEY_GUI_RIGHT: return 0xE7;
        
        // Если клавиша не найдена, вернуть 0
        default: return 0x00;
    }
}

static void update_keyboard_status(hid_keyboard_report_t const *report) {
    uint8_t keyboard_status = 0;   // Статус клавиатуры
    uint8_t extended_status = 0;   // Расширенный статус клавиатуры

    // Обрабатываем модификаторы
    if (report->modifier & 0x01) { // Left Control
        keyboard_status |= 0x01;  // Устанавливаем первый бит для Left Ctrl
    }
    if (report->modifier & 0x02) { // Left Shift
        keyboard_status |= 0x02;  // Устанавливаем второй бит для Left Shift
    }
    if (report->modifier & 0x04) { // Left Alt
        keyboard_status |= 0x04;  // Устанавливаем третий бит для Left Alt
    }
    if (report->modifier & 0x08) { // Left GUI (Windows/Command)
        keyboard_status |= 0x08;  // Устанавливаем четвёртый бит для Left GUI
    }
    if (report->modifier & 0x10) { // Right Control
        keyboard_status |= 0x10;  // Устанавливаем пятый бит для Right Ctrl
    }
    if (report->modifier & 0x20) { // Right Shift
        keyboard_status |= 0x20;  // Устанавливаем шестой бит для Right Shift
    }
    if (report->modifier & 0x40) { // Right Alt
        keyboard_status |= 0x40;  // Устанавливаем седьмой бит для Right Alt
    }
    if (report->modifier & 0x80) { // Right GUI
        keyboard_status |= 0x80;  // Устанавливаем восьмой бит для Right GUI
    }

    // Обработка Caps Lock
    if (report->modifier & 0x01) {
        // Проверяем состояние Caps Lock, например, можно использовать определённый бит
        extended_status |= 0x01;  // Устанавливаем Caps Lock активен
    }

    // Обработка Num Lock
    if (report->modifier & 0x02) {
        extended_status |= 0x02;  // Устанавливаем Num Lock активен
    }

    // Обработка Scroll Lock
    if (report->modifier & 0x04) {
        extended_status |= 0x04;  // Устанавливаем Scroll Lock активен
    }
 ///   x86_update_kbd_BDA(keyboard_status, extended_status);
}

inline static bool isInReport(hid_keyboard_report_t const *report, const unsigned char keycode) {
    for (unsigned char i: report->keycode) {
        if (i == keycode) {
            return true;
        }
    }
    return false;
}

void __not_in_flash_func(process_kbd_usb_report)(
    hid_keyboard_report_t const *report,
    hid_keyboard_report_t const *prev_report
) {
    // Модификаторы
    uint8_t modifier = report->modifier;  // Сохраняем текущие модификаторы (например, SHIFT, CTRL)
    bool numLock = modifier & 0x10;  // Чтение состояния NumLock, предполагается, что это 0x10
///    update_keyboard_status(report);

    // Обрабатываем каждый ключ в массиве keycode
    for (int i = 0; i < 6; i++) {
        uint8_t keycode = report->keycode[i];
        if (keycode == HID_KEY_NONE) {
            continue;  // Если код клавиши отсутствует (HID_KEY_NONE), пропускаем
        }
        if (isInReport(prev_report, keycode)) {
            continue;  // Уже добавляли в прошлое нажатие
        }

        // Преобразуем нажатую клавишу в соответствующие значения scan и ascii
        uint8_t scan = convert_keycode_to_scan(keycode);
        if (scan != 0) {
            // Добавляем в буфер
            x86_bios_process_key(scan);  // Функция для добавления в буфер
        }
    }
}

#if 0
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
#endif

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

void __time_critical_func(render_core)() {
    multicore_lockout_victim_init();
    clrBuf();
    graphics_init();
    const auto buffer = (uint8_t *)SCREEN;
    graphics_set_bgcolor(0x000000);
    graphics_set_offset(0, 0);
    graphics_set_flashmode(false, false);
    graphics_set_mode(TEXTMODE_DEFAULT);
    clrScr(7, 0);
    sem_acquire_blocking(&vga_start_semaphore);
    // 60 FPS loop
#define frame_tick (16666)
    uint64_t tick = time_us_64();
    uint64_t last_cursor_blink = tick;
    uint64_t last_frame_tick = tick;
    static uint32_t frame_no = 0;
    while (true) {
        if (tick >= last_frame_tick + frame_tick) {
            frame_no++;
#ifdef TFT
            refresh_lcd();
#endif
            last_frame_tick = tick;
          //  
        }
        if (tick >= last_cursor_blink + 500000) {
            cursor_blink_state ^= 1;
            last_cursor_blink = tick;
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
        case VREG_VOLTAGE_0_60: volt = "0.6 V"; break;
        case VREG_VOLTAGE_0_65: volt = "0.65V"; break;
        case VREG_VOLTAGE_0_70: volt = "0.7 V"; break;
        case VREG_VOLTAGE_0_75: volt = "0.75V"; break;
        case VREG_VOLTAGE_0_80: volt = "0.8 V"; break;
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
    }
    return volt;
}

#include <hardware/exception.h>

void sigbus(void) {
    goutf(30-1, true, "SIGBUS exception caught...");
    // reset_usb_boot(0, 0);
}
void __attribute__((naked, noreturn)) __printflike(1, 0) dummy_panic(__unused const char *fmt, ...) {
    goutf(30-2, true, "*** PANIC ***");
    if (fmt)
        goutf(30-1, true, fmt);
}

#define MB16 (16ul << 20)
#define MB8 (8ul << 20)
#define MB4 (4ul << 20)
#define MB1 (1ul << 20)
extern "C" uint32_t __not_in_flash_func(butter_psram_size)() {
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

static void format_hdd_test(int y) {
    u32 total_tracks = 512;          // Максимальное количество дорожек
    u32 heads = 256;                 // Максимальное количество головок
    u32 sectors_per_track = 63;      // Максимальное количество секторов на дорожку

    u8* buff = X86_FAR_PTR(X86_ES, 0x1000);  // Буфер для форматирования

    for (u32 track = 0; track < total_tracks; ++track) {
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

            u32 status = x86_int13_wrapper(eax, ebx, ecx, edx);

            goutf(y, false, "INT 13 AH=5 format [%d:%d:1-63] rc: %08X", track, head, status);
            if (status) return;
        }
    }
}

static void format_fdd(int y) {
    u8* buff = X86_FAR_PTR(X86_ES, 0x1000);
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
            u32 status = x86_int13_wrapper(eax, ebx, ecx, edx);
            goutf(y, false, "INT 13 AH=5 format [%d:%d:1-18] rc: %08X", track, head, status);
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

static FATFS fs;

int main() {
    volatile uint32_t *qmi_m0_timing=(uint32_t *)0x400d000c;
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_60);
    sleep_ms(33);
    *qmi_m0_timing = 0x60007204;
    set_sys_clock_khz(378 * KHZ, 0);
    *qmi_m0_timing = 0x60007303;

    psram_init(BUTTER_PSRAM_GPIO);
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, sigbus);

    int links = testPins(VGA_BASE_PIN, VGA_BASE_PIN + 1);
    SELECT_VGA = (links == 0) || (links == 0x1F);
    if (SELECT_VGA) {
        current_video_mode = 3;
        current_video_mode_width = 80;
        current_video_mode_height = 25;
    } else {
        current_video_mode = 0;
        current_video_mode_width = 40;
        current_video_mode_height = 25;
    }
    VGA_FRAMBUFFER_WINDOW_SIZE = current_video_mode_width * current_video_mode_height * 2;
    
    sem_init(&vga_start_semaphore, 0, 1);
    multicore_launch_core1(render_core);
    sem_release(&vga_start_semaphore);

    if (f_mount(&fs, "SD", 1) == FR_OK) {
        cd_card_mount = true;
        f_mkdir(HOME_DIR);
    }

    tuh_init(BOARD_TUH_RHPORT);
    ps2kbd.init_gpio();
//    keyboard_init();
    sleep_ms(50);

    uint32_t psram32 = butter_psram_size();
    double speedw, speedr;
    {
        uint32_t a = 0;
        uint32_t elapsed;
        uint32_t begin = time_us_32();
        double d = 1.0;
        uint32_t* p32 = (uint32_t*)PSRAM_DATA;
        for (; a < psram32 / sizeof(uint32_t); ++a) {
            p32[a] = a;
        }
        elapsed = time_us_32() - begin;
        speedw = d * a * sizeof(uint32_t) / elapsed;
        begin = time_us_32();
        for (a = 0; a < psram32 / sizeof(uint32_t); ++a) {
            if (a  != p32[a]) {
                // only for debug mode
                goutf(0, true, " PSRAM write/read test failed at %ph", p32 + a);
                break;
            }
        }
        elapsed = time_us_32() - begin;
        speedr = d * a * sizeof(uint32_t) / elapsed;
    }

    nespad_begin(clock_get_hz(clk_sys) / 1000, NES_GPIO_CLK, NES_GPIO_DATA, NES_GPIO_LAT);
    sleep_ms(50);
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
#endif

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    for (int i = 0; i < 6; i++) {
        sleep_ms(23);
        gpio_put(PICO_DEFAULT_LED_PIN, true);
        sleep_ms(23);
        gpio_put(PICO_DEFAULT_LED_PIN, false);
    }
    
    x86_init();
    x86_int10_wrapper(0x0003, 0, 0, 0); // try mode 3

    int y = 0;
    gprintf(y++, 12, 15, "Murmulator VGA/HDMI BIOS for RP2350 378 MHz 1.6V");
    gprintf(y++, 7, 0, "PSRAM (on GPIO-%d) %d MB %f/%f MBps", BUTTER_PSRAM_GPIO, psram32 >> 20, speedw, speedr);
    if (!cd_card_mount) {
        draw_text("SDCARD not connected", 0, y++, 12, 0);
    } else {
        gprintf(y++, 7, 0, "SDCARD %d FATs; %d free clusters (%d KB each)", fs.n_fats, f_getfree32(&fs), fs.csize >> 1);
    }
    {
        uint8_t rx[4];
        get_cpu_flash_jedec_id(rx);
        uint32_t flash_size = (1 << rx[3]);
        gprintf(y++, 7, 0, "FLASH %d MB; JEDEC ID: %02X-%02X-%02X-%02X", flash_size >> 20, rx[0], rx[1], rx[2], rx[3]);
    }
    // other PSRAM
    init_psram();
    uint32_t psram2_32 = psram_size();
    if (psram2_32) {
        uint8_t rx8[8];
        psram_id(rx8);
        gprintf(y++, 7, 0, "PSRAM #2 %d MB MFID:%02X KGD:%02X EID:%02X%02X-%02X%02X-%02X%02X",
            psram2_32 >> 20, rx8[0], rx8[1], rx8[2], rx8[3], rx8[4], rx8[5], rx8[6], rx8[7]
        );
        double d = 1.0;
        uint32_t a = 0;
        uint32_t elapsed;
        uint32_t begin = time_us_32();
        for (a = 0; a < psram32; a += 4) {
            write32psram(a, a);
        }
        elapsed = time_us_32() - begin;
        speedr = d * a / elapsed;
        begin = time_us_32();
        for (a = 0; a < psram32; a += 4) {
            if (a != read32psram(a)) {
                gprintf(y++, 12, 0, "PSRAM #2 read failed at %ph", a);
                break;
            }
        }
        elapsed = time_us_32() - begin;
        speedw = d * a / elapsed;
        gprintf(y++, 7, 0, "PSRAM #2 for EMS; W/R 32: %f/%f MBps", speedw, speedr);
    } else {
        gprintf(y++, 7, 0, "No PSRAM #2 for EMS detected");
    }
    u32 eax = x86_int10_wrapper(0x0F00, 0, 0, 0); // get videomode
    gprintf(y++, 7, 0, "Video mode: %d (%d columns)", (u8)eax, (u8)(eax >> 8));
    gprintf(y++, 7, 0, "Virtual 2xFDD 4xHDD (images in '/cross' folder)");

    u32 i = 0;
    while(1) {
        x86_translate_test();
//        eax = x86_int16_wrapper(0, 0, 0, 0);
  //      static int i = 0;
    //    u8 ascii = (u8)eax;
      //  u8 scan = (u8)(eax >> 8);
        //goutf(30-3, false, "R %02X[%c]/%02X (%d)", ascii, ascii ? ascii : '0', scan, i++);
    }

    __unreachable();
}
