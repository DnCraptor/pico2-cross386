// You may use, distribute and modify this code under the
// terms of the GPLv2 license, which unfortunately won't be
// written for another century.
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
// See:
// http://www.vetra.com/scancodes.html
// https://wiki.osdev.org/PS/2_Keyboard
//
#include "ps2kbd_mrmltr.h"
#if KBD_CLOCK_PIN == 2
#include "ps2kbd_mrmltr2.pio.h"
#else
#include "ps2kbd_mrmltr.pio.h"
#endif
#include "hardware/clocks.h"

#include "ports.h"
#include "8259A.h"

#ifdef DEBUG_PS2
#define DBG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DBG_PRINTF(...)
#endif

#define HID_KEYBOARD_REPORT_MAX_KEYS 6

// PS/2 set 2 to HID key conversion
static uint8_t ps2kbd_page_0[] {
  /* 00 (  0) */ HID_KEY_NONE,
  /* 01 (  1) */ HID_KEY_F9,
  /* 02 (  2) */ 0x00,
  /* 03 (  3) */ HID_KEY_F5,
  /* 04 (  4) */ HID_KEY_F3,
  /* 05 (  5) */ HID_KEY_F1,
  /* 06 (  6) */ HID_KEY_F2,
  /* 07 (  7) */ HID_KEY_F12,
  /* 08 (  8) */ HID_KEY_F13,
  /* 09 (  9) */ HID_KEY_F10,
  /* 0A ( 10) */ HID_KEY_F8,
  /* 0B ( 11) */ HID_KEY_F6,
  /* 0C ( 12) */ HID_KEY_F4,
  /* 0D ( 13) */ HID_KEY_TAB,
  /* 0E ( 14) */ HID_KEY_GRAVE,
  /* 0F ( 15) */ HID_KEY_KEYPAD_EQUAL,
  /* 10 ( 16) */ HID_KEY_F14,
  /* 11 ( 17) */ HID_KEY_ALT_LEFT,
  /* 12 ( 18) */ HID_KEY_SHIFT_LEFT,
  /* 13 ( 19) */ 0x00,
  /* 14 ( 20) */ HID_KEY_CONTROL_LEFT,
  /* 15 ( 21) */ HID_KEY_Q,
  /* 16 ( 22) */ HID_KEY_1,
  /* 17 ( 23) */ 0x00,
  /* 18 ( 24) */ HID_KEY_F15,
  /* 19 ( 25) */ 0x00,
  /* 1A ( 26) */ HID_KEY_Z,
  /* 1B ( 27) */ HID_KEY_S,
  /* 1C ( 28) */ HID_KEY_A,
  /* 1D ( 29) */ HID_KEY_W,
  /* 1E ( 30) */ HID_KEY_2,
  /* 1F ( 31) */ 0x00,
  /* 20 ( 32) */ HID_KEY_F16,
  /* 21 ( 33) */ HID_KEY_C,
  /* 22 ( 34) */ HID_KEY_X,
  /* 23 ( 35) */ HID_KEY_D,
  /* 24 ( 36) */ HID_KEY_E,
  /* 25 ( 37) */ HID_KEY_4,
  /* 26 ( 38) */ HID_KEY_3,
  /* 27 ( 39) */ 0x00,
  /* 28 ( 40) */ HID_KEY_F17,
  /* 29 ( 41) */ HID_KEY_SPACE,
  /* 2A ( 42) */ HID_KEY_V,
  /* 2B ( 43) */ HID_KEY_F,
  /* 2C ( 44) */ HID_KEY_T,
  /* 2D ( 45) */ HID_KEY_R,
  /* 2E ( 46) */ HID_KEY_5,
  /* 2F ( 47) */ 0x00,
  /* 30 ( 48) */ HID_KEY_F18,
  /* 31 ( 49) */ HID_KEY_N,
  /* 32 ( 50) */ HID_KEY_B,
  /* 33 ( 51) */ HID_KEY_H,
  /* 34 ( 52) */ HID_KEY_G,
  /* 35 ( 53) */ HID_KEY_Y,
  /* 36 ( 54) */ HID_KEY_6,
  /* 37 ( 55) */ 0x00,
  /* 38 ( 56) */ HID_KEY_F19,
  /* 39 ( 57) */ 0x00,
  /* 3A ( 58) */ HID_KEY_M,
  /* 3B ( 59) */ HID_KEY_J,
  /* 3C ( 60) */ HID_KEY_U,
  /* 3D ( 61) */ HID_KEY_7,
  /* 3E ( 62) */ HID_KEY_8,
  /* 3F ( 63) */ 0x00,
  /* 40 ( 64) */ HID_KEY_F20,
  /* 41 ( 65) */ HID_KEY_COMMA,
  /* 42 ( 66) */ HID_KEY_K,
  /* 43 ( 67) */ HID_KEY_I,
  /* 44 ( 68) */ HID_KEY_O,
  /* 45 ( 69) */ HID_KEY_0,
  /* 46 ( 70) */ HID_KEY_9,
  /* 47 ( 71) */ 0x00,
  /* 48 ( 72) */ HID_KEY_F21,
  /* 49 ( 73) */ HID_KEY_PERIOD,
  /* 4A ( 74) */ HID_KEY_SLASH,
  /* 4B ( 75) */ HID_KEY_L,
  /* 4C ( 76) */ HID_KEY_SEMICOLON,
  /* 4D ( 77) */ HID_KEY_P,
  /* 4E ( 78) */ HID_KEY_MINUS,
  /* 4F ( 79) */ 0x00,
  /* 50 ( 80) */ HID_KEY_F22,
  /* 51 ( 81) */ 0x00,
  /* 52 ( 82) */ HID_KEY_APOSTROPHE,
  /* 53 ( 83) */ 0x00,
  /* 54 ( 84) */ HID_KEY_BRACKET_LEFT,
  /* 55 ( 85) */ HID_KEY_EQUAL,
  /* 56 ( 86) */ 0x00,
  /* 57 ( 87) */ HID_KEY_F23,
  /* 58 ( 88) */ HID_KEY_CAPS_LOCK,
  /* 59 ( 89) */ HID_KEY_SHIFT_RIGHT,
  /* 5A ( 90) */ HID_KEY_ENTER, // RETURN ??
  /* 5B ( 91) */ HID_KEY_BRACKET_RIGHT,
  /* 5C ( 92) */ 0x00,
  /* 5D ( 93) */ HID_KEY_EUROPE_1,
  /* 5E ( 94) */ 0x00,
  /* 5F ( 95) */ HID_KEY_F24,
  /* 60 ( 96) */ 0x00,
  /* 61 ( 97) */ HID_KEY_EUROPE_2,
  /* 62 ( 98) */ 0x00,
  /* 63 ( 99) */ 0x00,
  /* 64 (100) */ 0x00,
  /* 65 (101) */ 0x00,
  /* 66 (102) */ HID_KEY_BACKSPACE,
  /* 67 (103) */ 0x00,
  /* 68 (104) */ 0x00,
  /* 69 (105) */ HID_KEY_KEYPAD_1,
  /* 6A (106) */ 0x00,
  /* 6B (107) */ HID_KEY_KEYPAD_4,
  /* 6C (108) */ HID_KEY_KEYPAD_7,
  /* 6D (109) */ 0x00,
  /* 6E (110) */ 0x00,
  /* 6F (111) */ 0x00,
  /* 70 (112) */ HID_KEY_KEYPAD_0,
  /* 71 (113) */ HID_KEY_KEYPAD_DECIMAL,
  /* 72 (114) */ HID_KEY_KEYPAD_2,
  /* 73 (115) */ HID_KEY_KEYPAD_5,
  /* 74 (116) */ HID_KEY_KEYPAD_6,
  /* 75 (117) */ HID_KEY_KEYPAD_8,
  /* 76 (118) */ HID_KEY_ESCAPE,
  /* 77 (119) */ HID_KEY_NUM_LOCK,
  /* 78 (120) */ HID_KEY_F11,
  /* 79 (121) */ HID_KEY_KEYPAD_ADD,
  /* 7A (122) */ HID_KEY_KEYPAD_3,
  /* 7B (123) */ HID_KEY_KEYPAD_SUBTRACT,
  /* 7C (124) */ HID_KEY_KEYPAD_MULTIPLY,
  /* 7D (125) */ HID_KEY_KEYPAD_9,
  /* 7E (126) */ HID_KEY_SCROLL_LOCK,
  /* 7F (127) */ 0x00,
  /* 80 (128) */ 0x00,
  /* 81 (129) */ 0x00,
  /* 82 (130) */ 0x00,
  /* 83 (131) */ HID_KEY_F7
};


Ps2Kbd_Mrmltr::Ps2Kbd_Mrmltr(PIO pio, uint base_gpio, std::function<void(hid_keyboard_report_t *curr, hid_keyboard_report_t *prev)> keyHandler) :
  _pio(pio),
  _base_gpio(base_gpio),
  _double(false),
  _overflow(false),
  _keyHandler(keyHandler)
{
  clearHidKeys();
  clearActions();
}

void Ps2Kbd_Mrmltr::clearHidKeys() {
  _report.modifier = 0;
  for (int i = 0; i < HID_KEYBOARD_REPORT_MAX_KEYS; ++i) _report.keycode[i] = HID_KEY_NONE;
}

inline static uint8_t hidKeyToMod(uint8_t hidKeyCode) {
  uint8_t m = 0;
  switch(hidKeyCode) {
  case HID_KEY_CONTROL_LEFT: m = KEYBOARD_MODIFIER_LEFTCTRL; break;
  case HID_KEY_SHIFT_LEFT: m = KEYBOARD_MODIFIER_LEFTSHIFT; break;
  case HID_KEY_ALT_LEFT: m = KEYBOARD_MODIFIER_LEFTALT; break;
  case HID_KEY_GUI_LEFT: m = KEYBOARD_MODIFIER_LEFTGUI; break;
  case HID_KEY_CONTROL_RIGHT: m = KEYBOARD_MODIFIER_RIGHTCTRL; break;
  case HID_KEY_SHIFT_RIGHT: m = KEYBOARD_MODIFIER_RIGHTSHIFT; break;
  case HID_KEY_ALT_RIGHT: m = KEYBOARD_MODIFIER_RIGHTALT; break;
  case HID_KEY_GUI_RIGHT: m = KEYBOARD_MODIFIER_RIGHTGUI; break;
  default: break;
  } 
  return m;
}

void Ps2Kbd_Mrmltr::handleHidKeyPress(uint8_t hidKeyCode) {
  if (!_keyHandler) return;
  hid_keyboard_report_t prev = _report;
  
  // Check the key is not alreay pressed
  for (int i = 0; i < HID_KEYBOARD_REPORT_MAX_KEYS; ++i) {
    if (_report.keycode[i] == hidKeyCode) {
      return;
    }
  }
  
  _report.modifier |= hidKeyToMod(hidKeyCode);
  
  for (int i = 0; i < HID_KEYBOARD_REPORT_MAX_KEYS; ++i) {
    if (_report.keycode[i] == HID_KEY_NONE) {
      _report.keycode[i] = hidKeyCode;      
      _keyHandler(&_report, &prev);
      return;
    }
  }
  
  // TODO Overflow
  DBG_PRINTF("PS/2 keyboard HID overflow\n");
}

void Ps2Kbd_Mrmltr::handleHidKeyRelease(uint8_t hidKeyCode) {
  if (!_keyHandler) return;
  hid_keyboard_report_t prev = _report;
  
  _report.modifier &= ~hidKeyToMod(hidKeyCode);
  
  for (int i = 0; i < HID_KEYBOARD_REPORT_MAX_KEYS; ++i) {
    if (_report.keycode[i] == hidKeyCode) {
      _report.keycode[i] = HID_KEY_NONE;
      _keyHandler(&_report, &prev);
      return;
    }
  }
}

uint8_t Ps2Kbd_Mrmltr::hidCodePage0(uint8_t ps2code) {
  return ps2code < sizeof(ps2kbd_page_0) ? ps2kbd_page_0[ps2code] : HID_KEY_NONE;
}

// PS/2 set 2 after 0xe0 to HID key conversion
uint8_t Ps2Kbd_Mrmltr::hidCodePage1(uint8_t ps2code) {
  switch(ps2code) {
// TODO these belong to a different HID usage page
//  case 0x37: return HID_KEY_POWER;
//  case 0x3f: return HID_KEY_SLEEP;
//  case 0x5e: return HID_KEY_WAKE;
  case 0x11: return HID_KEY_ALT_RIGHT;
  case 0x1f: return HID_KEY_GUI_LEFT;
  case 0x14: return HID_KEY_CONTROL_RIGHT;
  case 0x27: return HID_KEY_GUI_RIGHT;
  case 0x4a: return HID_KEY_KEYPAD_DIVIDE;
  case 0x5a: return HID_KEY_KEYPAD_ENTER;
  case 0x69: return HID_KEY_END;
  case 0x6b: return HID_KEY_ARROW_LEFT;
  case 0x6c: return HID_KEY_HOME;
  case 0x7c: return HID_KEY_PRINT_SCREEN;
  case 0x70: return HID_KEY_INSERT;
  case 0x71: return HID_KEY_DELETE;
  case 0x72: return HID_KEY_ARROW_DOWN;
  case 0x74: return HID_KEY_ARROW_RIGHT;
  case 0x75: return HID_KEY_ARROW_UP;
  case 0x7a: return HID_KEY_PAGE_DOWN;
  case 0x7d: return HID_KEY_PAGE_UP;

  default: 
    return HID_KEY_NONE;
  }
}

#include "ff.h"

void Ps2Kbd_Mrmltr::handleActions() {
  /*
  FIL f;
  f_open(&f, "1.log", FA_OPEN_APPEND | FA_WRITE);
  char tmp[64];
  for (uint i = 0; i <= _action; ++i) {
    snprintf(tmp, 64, "PS/2 key %s (i: %d) page %2.2X (%3.3d) code %2.2X (%3.3d)\n",
      _actions[i].release ? "release" : "press", i,
      _actions[i].page,
      _actions[i].page,
      _actions[i].code,
      _actions[i].code);
    UINT bw;
    f_write(&f, tmp, strlen(tmp), &bw); 
  }
  f_close(&f);
  */
  uint8_t hidCode;
  bool release;
  if (_action == 0) {
    switch (_actions[0].page) {
      case 1: {
        hidCode = hidCodePage1(_actions[0].code);
        break;
      }
      default: {
        hidCode = hidCodePage0(_actions[0].code);
        break;
      }
    }
    release = _actions[0].release;
  }
  else {
    if (_action == 1 && _actions[0].code == 0x14 && _actions[1].code == 0x77) {
       hidCode = HID_KEY_PAUSE;
       release = _actions[0].release;
    } else {
    // TODO get the HID code for extended PS/2 codes
      hidCode = HID_KEY_NONE;
      release = false;
    }
  }
  
  if (hidCode != HID_KEY_NONE) {
    
    DBG_PRINTF("HID key %s code %2.2X (%3.3d)\n",
      release ? "release" : "press",
      hidCode,
      hidCode);
      
    if (release) {
      handleHidKeyRelease(hidCode);
    }
    else {
      handleHidKeyPress(hidCode);
    }
  }
  
  DBG_PRINTF("PS/2 HID m=%2X ", _report.modifier);
  #ifdef DEBUG_PS2
  for (int i = 0; i < HID_KEYBOARD_REPORT_MAX_KEYS; ++i) printf("%2X ", _report.keycode[i]);
  printf("\n");
  #endif
}

const static u8 at2xt[0x80] = {
  0,
  0x43, // 0x01 F9
  0, // 0x02 ?
  0x3F, // 0x03 F5
  0x3D, // 0x04 F3
  0x3B, // 0x05 F1
  0x3C, // 0x06 F2
  0x58, // 0x07 F12
  0, // 0x08 
  0x44, // 0x09 F10
  0x42, // 0x0A F8
  0x40, // 0x0B F6
  0x3E, // 0x0C F4
  0x0F, // 0x0D TAB
  0x29, // 0x0E ~
  0, // 0x0F 
  0x65, // 0x10 Поиск
  0x38, // 0x11 L ALT
  0x2A, // 0x12 L SHIFT
  0, // 0x13
  0x1D, // 0x14 L CTR
  0x10, // 0x15 Q
  0x02, // 0x16 1
  0, // 0x17 
  0x66, // 0x18 Закладки
  0, // 0x19
  0x2C, // 0x1A Z
  0x1F, // 0x1B S
  0x1E, // 0x1C A
  0x11, // 0x1D W
  0x03, // 0x1E 2
  0x5B, // 0x1F L WIN
  0x67, // 0x20 Обновить
  0x2E, // 0x21 C
  0x2D, // 0x22 X
  0x20, // 0x23 D
  0x12, // 0x24 E
  0x05, // 0x25 4
  0x04, // 0x26 3
  0x5C, // 0x27 R WIN
  0x68, // 0x28 Стоп
  0x39, // 0x29 SPACE
  0x2F, // 0x2A V
  0x21, // 0x2B F
  0x14, // 0x2C T
  0x13, // 0x2D R
  0x06, // 0x2E 5
  0x5D, // 0x2F MENU
  0x69, // 0x30 Вперед
  0x31, // 0x31 N
  0x30, // 0x32 B
  0x23, // 0x33 H
  0x22, // 0x34 G
  0x15, // 0x35 Y
  0x07, // 0x36 6
  0x5E, // 0x37 Power
  0x6A, // 0x38 Назад
  0, // 0x39
  0x32, // 0x3A M
  0x24, // 0x3B J
  0x16, // 0x3C U
  0x08, // 0x3D 7
  0x09, // 0x3E 8
  0x5F, // 0x3F Sleep
  0x6B, // 0x40 Мой компьютер
  0x33, // 0x41 ,
  0x25, // 0x42 K
  0x17, // 0x43 I
  0x18, // 0x44 O
  0x0B, // 0x45 0
  0x0A, // 0x46 9
  0, // 0x47 
  0x6C, // 0x48 Электронная почта
  0x34, // 0x49 .
  0x35, // 0x4A /
  0x26, // 0x4B L
  0x27, // 0x4C ;
  0x19, // 0x4D P
  0x0C, // 0x4E -
  0, // 0x4F 
  0x6D, // 0x50 Media Select
  0, // 0x51 
  0x28, // 0x52 '
  0, // 0x53 
  0x1A, // 0x54 [
  0x0D, // 0x55 =
  0, // 0x56 
  0, // 0x57 
  0x3A, // 0x58 CAPS LOCK
  0x36, // 0x59 R SHIFT
  0x1C, // 0x5A ENTER
  0x1B, // 0x5B ]
  0, // 0x5C
  0x2B, // 0x5D BACKSLASH
  0x63, // 0x5E Wake
  0, // 0x5F
  0, // 0x60
  0, // 0x61
  0, // 0x62
  0, // 0x63
  0, // 0x64
  0, // 0x65
  0x0E, // 0x66 BS
  0, // 0x67
  0, // 0x68
  0x4F, // 0x69 1
  0, // 0x6A
  0x4B, // 0x6B 4
  0x47, // 0x6C 7
  0, // 0x6D 
  0, // 0x6E
  0, // 0x6F
  0x52, // 0x70 0
  0x53, // 0x71 .
  0x50, // 0x72 2
  0x4C, // 0x73 5
  0x4D, // 0x74 6
  0x48, // 0x75 8
  0x01, // 0x76 ESC
  0x45, // 0x77 NumLock
  0x57, // 0x78 F11
  0x4E, // 0x79 +
  0x51, // 0x7A 3
  0x4A, // 0x7B -
  0x37, // 0x7C *
  0x49, // 0x7D 9
  0x46, // 0x7E Scroll Lock
  0, // 0x7F
};

inline static u8 toXTcode(u8 at, bool f0mode) {
  if (at == 0xE0) return at;
  if (at < 0x80)
    return f0mode ? (at2xt[at] | 0x80) : at2xt[at];
  return at2xt[at & 0x7F] | 0x80;
}

void Ps2Kbd_Mrmltr::tick() {
  if (pio_sm_is_rx_fifo_full(_pio, _sm)) {
    //goutf(1, false, "PS/2 keyboard PIO overflow");
    _overflow = true;
    while (!pio_sm_is_rx_fifo_empty(_pio, _sm)) {
      // pull a scan code from the PIO SM fifo
      uint32_t rc = _pio->rxf[_sm];
    //  goutf(30-1, false, "PS/2 drain rc %4.4lX", rc);
    }
 ///   clearHidKeys();
 ///   clearActions();
  }

  static bool f0mode = false;
  while (!pio_sm_is_rx_fifo_empty(_pio, _sm)) {
    // pull a scan code from the PIO SM fifo
    uint32_t rc = _pio->rxf[_sm];    
    uint32_t code = (rc << 2) >> 24;

    if (code == 0xF0) {
      f0mode = true;
      continue;
    }
    if (X86_PORTS[0x62] & 2) {
      // ignore - prev is not yet read/ready
    } else {
      X86_PORTS[0x60] = toXTcode((u8)code, f0mode);
      X86_PORTS[0x62] |= 2;
      X86_IRQ1();
    }
    f0mode = false;
  }
}

Ps2Kbd_Mrmltr ps2kbd(
  pio1,
  KBD_CLOCK_PIN,
  0 // TODO: process_kbd_usb_report
);

static void __not_in_flash_func() KeyboardHandler(void) {
  ps2kbd.tick();
}

// TODO Error checking and reporting
void Ps2Kbd_Mrmltr::init_gpio() {
    // init KBD pins to input
    gpio_init(_base_gpio);     // Data
    gpio_init(_base_gpio + 1); // Clock
    // with pull up
    gpio_pull_up(_base_gpio);
    gpio_pull_up(_base_gpio + 1);
    // get a state machine
    _sm = pio_claim_unused_sm(_pio, true);
    // reserve program space in SM memory
#if KBD_CLOCK_PIN == 2
    uint offset = pio_add_program(_pio, &m2ps2kbd_program);
#else
    uint offset = pio_add_program(_pio, &ps2kbd_program);
#endif
    // Set pin directions base
    pio_sm_set_consecutive_pindirs(_pio, _sm, _base_gpio, 2, false);
    // program the start and wrap SM registers
#if KBD_CLOCK_PIN == 2
    pio_sm_config c = m2ps2kbd_program_get_default_config(offset);
#else
    pio_sm_config c = ps2kbd_program_get_default_config(offset);
#endif
    // Set the base input pin. pin index 0 is DAT, index 1 is CLK  // Murmulator: 0->CLK 1->DAT ( _base_gpio + 1)
    //  sm_config_set_in_pins(&c, _base_gpio);
    sm_config_set_in_pins(&c, _base_gpio + 1);
    // Shift 8 bits to the right, autopush enabled
    sm_config_set_in_shift(&c, true, true, 10);
    // Deeper FIFO as we're not doing any TX
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    // We don't expect clock faster than 16.7KHz and want no less
    // than 8 SM cycles per keyboard clock.
    float div = (float)clock_get_hz(clk_sys) / (8 * 16700);
    sm_config_set_clkdiv(&c, div);
    // Ready to go
    pio_sm_init(_pio, _sm, offset, &c);
    pio_sm_set_enabled(_pio, _sm, true);

    gpio_set_irq_enabled_with_callback(_base_gpio, GPIO_IRQ_EDGE_RISE, true, (gpio_irq_callback_t)&KeyboardHandler);
}
