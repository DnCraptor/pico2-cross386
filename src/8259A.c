// 8259A PIC (Master + Slave)
#include "8259A.h"
#include "ports.h"
#include "graphics.h"

/*
# Инициализация 8259A PIC (Master + Slave)

## 1. Отключить прерывания на процессоре
Чтобы ничего не мешало инициализации:

```assembly
cli         ; запретить прерывания
```

---

## 2. Отправить ICW1 — команду начала инициализации

- На порт **0x20** — Master PIC
- На порт **0xA0** — Slave PIC
- Значение ICW1:
  - `0001 0001b` = `0x11`
    - Начинаем инициализацию
    - ICW4 будет передана
    - Рабочий режим: каскадный (есть Master+Slave)

```assembly
mov al, 0x11
out 0x20, al    ; Master PIC
out 0xA0, al    ; Slave PIC
```

---

## 3. Отправить ICW2 — базовый вектор прерываний

- Для Master PIC: базовый вектор 0x20 (INT 20h..27h для IRQ0..7)
- Для Slave PIC: базовый вектор 0x28 (INT 28h..2Fh для IRQ8..15)

```assembly
mov al, 0x20
out 0x21, al    ; Master PIC: IRQ0 -> INT 20h
mov al, 0x28
out 0xA1, al    ; Slave PIC: IRQ8 -> INT 28h
```

---

## 4. Отправить ICW3 — настройка каскадных связей

- Master PIC: указывает, на каком IRQ подвешен Slave PIC (обычно IRQ2 = бит 2 = 00000100b = `0x04`)
- Slave PIC: указывает свой номер линии на Master'е (обычно 2)

```assembly
mov al, 0x04
out 0x21, al    ; Master: Slave на IRQ2
mov al, 0x02
out 0xA1, al    ; Slave: подключен к IRQ2
```

---

## 5. Отправить ICW4 — режим работы

- `0000 0001b` = `0x01`
  - 8086/88 Mode (стандартный режим для DOS)

```assembly
mov al, 0x01
out 0x21, al    ; Master: режим 8086/88
out 0xA1, al    ; Slave: режим 8086/88
```

---

## 6. Установить маски разрешения IRQ

В начале обычно маскируются все IRQ, а потом постепенно разрешаются нужные:

```assembly
mov al, 0xFF
out 0x21, al    ; Master: все IRQ запрещены
out 0xA1, al    ; Slave: все IRQ запрещены
```

Позже, в зависимости от задач, маска может быть изменена (разрешить таймер, клавиатуру и т.п.).

---

## 7. Включить прерывания обратно на процессоре

```assembly
sti         ; разрешить прерывания
```

---

# Полный минимальный код настройки PIC
```assembly
cli                     ; Запретить прерывания

mov al, 0x11
out 0x20, al            ; Master ICW1
out 0xA0, al            ; Slave ICW1

mov al, 0x20
out 0x21, al            ; Master ICW2 (base vector 0x20)
mov al, 0x28
out 0xA1, al            ; Slave ICW2 (base vector 0x28)

mov al, 0x04
out 0x21, al            ; Master ICW3 (Slave на IRQ2)
mov al, 0x02
out 0xA1, al            ; Slave ICW3 (подключение к IRQ2 Master'а)

mov al, 0x01
out 0x21, al            ; Master ICW4 (8086 mode)
out 0xA1, al            ; Slave ICW4 (8086 mode)

mov al, 0xFF
out 0x21, al            ; Маскировать все IRQ на Master
out 0xA1, al            ; Маскировать все IRQ на Slave

sti                     ; Разрешить прерывания
```

---

# Краткий итог:
| Этап | Что настраивается |
|:-----|:------------------|
| ICW1 | Начало инициализации |
| ICW2 | Базовые векторы прерываний |
| ICW3 | Каскадная связь Master/Slave |
| ICW4 | Режим работы PIC (8086) |
| OCW1 | Маска разрешённых IRQ |

*/

void x86_8259A_Master_PIC(u8 v) {

}

void x86_8259A_Slave_PIC(u8 v) {
    
}

void x86_8259A_Master_PIC_D(u8 v) {

}

void x86_8259A_Slave_PIC_D(u8 v) {
    
}

/*
[Keyboard] ---> [8042 Controller] ---> [IRQ1 Trigger] ---> [CPU INT 09h Handler]
                                                             |
                                             [Read Port 0x60 -> Buffer]
                                                             |
                                               [Program calls INT 16h]
                                                             |
                                               [Read symbol from buffer]

*/

void __time_critical_func() X86_IRQ1(void) {
    goutf(30-1, false, "W %02X", X86_PORTS[0x60]);
    X86_PORTS[0x62] &= ~2;
}
