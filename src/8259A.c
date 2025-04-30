// 8259A PIC (Master + Slave)
#include "8259A.h"
#include "ports.h"
#include "graphics.h"

/*
Примеры команд:
Инициализация стандартного Master 8259A (x86):

out 0x20, 0x11 ; ICW1: Начинаем инициализацию, ожидаем ICW4
out 0x21, 0x08 ; ICW2: Базовый вектор прерываний 08h
out 0x21, 0x04 ; ICW3: Подчиненный контроллер на IRQ2
out 0x21, 0x01 ; ICW4: Режим 8086/88
EOI (End Of Interrupt):

out 0x20, 0x20 ; OCW2: Послать EOI на мастер PIC

Маскирование всех IRQ кроме IRQ0 (таймер):
out 0x21, 0xFE ; OCW1: Маска (биты: 11111110)

OCW1 (Interrupt Mask Register, IMR)
Порт: 0x21 (Master), 0xA1 (Slave)

IRR (Interrupt Request Register)
Показывает активные запросы IRQ, ожидающие обработки
Нельзя читать напрямую!
Сначала нужно отправить OCW3 команду, чтобы выбрать IRR как источник чтения.
Последовательность:
mov al, 0x0A     ; OCW3: RR=1 (чтение), RIS=1 (IRR)
out 0x20, al
in  al, 0x20     ; Считать IRR

ISR (In-Service Register)
Показывает, какие IRQ сейчас обслуживаются
Аналогично IRR, нужен OCW3 с другим флагом:
mov al, 0x0B     ; OCW3: RR=1, RIS=0 (ISR)
out 0x20, al
in  al, 0x20     ; Считать ISR

 Полинг (Polling Mode)
Можно прочитать OCW3 в polling режиме, когда CPU сам спрашивает PIC, а не работает через IRQ.
mov al, 0x0C     ; OCW3: Poll=1
out 0x20, al
in  al, 0x20     ; Получить номер активного IRQ (если есть)
*/

static u8 master_mask = 0xFF;
static u8 slave_mask = 0xFF;
static u8 master_vector_offset = 0;
static u8 slave_vector_offset = 0;
static u8 master_icw_step = 0;
static u8 slave_icw_step = 0;
static u8 master_isr = 0;
static u8 slave_isr = 0;
static u8 master_irr = 0;
static u8 slave_irr = 0;
static u8 master_ocw3_cmd = 0;
static u8 slave_ocw3_cmd = 0;
static u8 master_icw3 = 0;
static u8 slave_icw3 = 0;

#define ICW1_INIT         0x10
#define ICW1_ICW4         0x01
#define ICW4_8086         0x01
#define OCW2_EOI          0x20
#define OCW3_READ_ISR     0x0B
#define OCW3_READ_IRR     0x0A
#define OCW3_MASK         0xF8
#define OCW3_ID           0x08
#define OCW2_SL           0x40
#define OCW2_IRQ          0x07

static void __time_critical_func() x86_8259A_poll(void) {
    // Проверка IRR -> ISR и генерация INT
    for (int i = 0; i < 8; i++) {
        if (master_irr & (1 << i)) {
            if (!(master_mask & (1 << i))) {
                master_irr &= ~(1 << i);
                master_isr |= (1 << i);
                if (i == (master_icw3 & 0x07)) { // Cascade to slave
                    for (int j = 0; j < 8; j++) {
                        if (slave_irr & (1 << j)) {
                            if (!(slave_mask & (1 << j))) {
                                slave_irr &= ~(1 << j);
                                slave_isr |= (1 << j);
                                u32 v4 = (uint32_t)X86_FAR_PTR(0, (slave_vector_offset + i) << 2);
                                x86_raise_interrupt_wrapper(v4);
                                return;
                            }
                        }
                    }
                } else {
                    u32 v4 = (uint32_t)X86_FAR_PTR(0, (master_vector_offset + i) << 2);
                    x86_raise_interrupt_wrapper(v4);
                    return;
                }
            }
        }
    }
}

void __time_critical_func() x86_8259A_raise_irq(u8 irq) {
    if (irq < 8)
        master_irr |= (1 << irq);
    else if (irq < 16)
        slave_irr |= (1 << (irq - 8));
    x86_8259A_poll();
}

void x86_8259A_Master_PIC(u8 v) {
  if (v & ICW1_INIT) {
      master_icw_step = 1;
      X86_PORTS[0x21] = master_mask = 0xFF; // маска по умолчанию — всё запрещено
  } else {
      // Только после полной инициализации можно читать IRR/ISR
      if ((master_ocw3_cmd & 0x0F) == OCW3_READ_ISR) {
          X86_PORTS[0x20] = master_isr;
      } else if ((master_ocw3_cmd & 0x0F) == OCW3_READ_IRR) {
          X86_PORTS[0x20] = master_irr;
      }

      // OCW3 обработка
      if ((v & OCW3_MASK) == OCW3_ID) {
          master_ocw3_cmd = v;
      }
      // OCW2: EOI
      else if (v & OCW2_EOI) {
          if (v & OCW2_SL) {
              master_isr &= ~(1 << (v & OCW2_IRQ)); // конкретный IRQ
          } else {
              for (int i = 0; i < 8; ++i) {
                  if (master_isr & (1 << i)) {
                      master_isr &= ~(1 << i); // первый установленный бит
                      break;
                  }
              }
          }
      }
  }
}

void x86_8259A_Master_PIC_D(u8 v) {
  if (master_icw_step == 1) {
      master_vector_offset = v; // ICW2
      master_icw_step++;
  } else if (master_icw_step == 2) {
      master_icw3 = v; // ICW3
      master_icw_step++;
  } else if (master_icw_step == 3) {
      if (v & ICW4_8086) {
          // режим 8086 подтвержден
      }
      master_icw_step = 0;
  } else {
      // запись маски
      X86_PORTS[0x21] = master_mask = v;
      x86_8259A_poll();
  }
}

void x86_8259A_Slave_PIC(u8 v) {
  if (v & ICW1_INIT) {
      slave_icw_step = 1;
      X86_PORTS[0xA1] = slave_mask = 0xFF; // запретить все IRQ по умолчанию
  } else {
      // Чтение IRR/ISR только после полной инициализации
      if ((slave_ocw3_cmd & 0x0F) == OCW3_READ_ISR) {
          X86_PORTS[0xA0] = slave_isr;
      } else if ((slave_ocw3_cmd & 0x0F) == OCW3_READ_IRR) {
          X86_PORTS[0xA0] = slave_irr;
      }

      // OCW3
      if ((v & OCW3_MASK) == OCW3_ID) {
          slave_ocw3_cmd = v;
      }
      // OCW2: EOI
      else if (v & OCW2_EOI) {
          if (v & OCW2_SL) {
              slave_isr &= ~(1 << (v & OCW2_IRQ)); // конкретный IRQ
          } else {
              for (int i = 0; i < 8; ++i) {
                  if (slave_isr & (1 << i)) {
                      slave_isr &= ~(1 << i);
                      break;
                  }
              }
          }
      }
  }
}

void x86_8259A_Slave_PIC_D(u8 v) {
  if (slave_icw_step == 1) {
      slave_vector_offset = v; // ICW2
      slave_icw_step++;
  } else if (slave_icw_step == 2) {
      slave_icw3 = v; // ICW3
      slave_icw_step++;
  } else if (slave_icw_step == 3) {
      if (v & ICW4_8086) {
          // режим 8086/88 подтвержден
      }
      slave_icw_step = 0;
  } else {
      // запись маски
      X86_PORTS[0xA1] = slave_mask = v;
      x86_8259A_poll();
  }
}

void __time_critical_func() X86_IRQ1(void) {
  x86_8259A_raise_irq(1);
}
void __time_critical_func() X86_IRQ2(void) {
  x86_8259A_raise_irq(2);
}
void __time_critical_func() X86_IRQ3(void) {
  x86_8259A_raise_irq(3);
}
void __time_critical_func() X86_IRQ4(void) {
  x86_8259A_raise_irq(4);
}
void __time_critical_func() X86_IRQ5(void) {
  x86_8259A_raise_irq(5);
}
void __time_critical_func() X86_IRQ6(void) {
  x86_8259A_raise_irq(6);
}
void __time_critical_func() X86_IRQ7(void) {
  x86_8259A_raise_irq(7);
}
