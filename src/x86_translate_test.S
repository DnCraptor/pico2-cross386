.syntax unified
.thumb

.global x86_translate_test
.type x86_translate_test, %function

x86_translate_test:
    push {r0-r12, lr}         // Сохраняем рабочие регистры (r1-r12, lr)
start_it:
    AND R4, R4, #0xFFFF00FF   // x86 00000000: B4h 00h = MOV AH, 0 - Обнуляем старший байт
    CPSID i                   // Запрет прерываний перед INT
    adr  r11, m00000004       // x86 00000002: CCh = int 16
    mrs  r12, apsr            // Сохраняем флаги
    push {r11, r12}           // Эмулируем PUSH IP, PUSH FLAGS
    ldr  r11, =0x11000058     // Адрес обработчика INT 16h
    ldr  r11, [r11]           // Адрес обработчика (IDT)
    mov  pc, r11              // Переход к обработчику
m00000004:
    LDR  R0, =0x0E            // x86 00000004: B4h 0Eh = MOV AH, 0x0E
    LSL  R0, R0, #8           // imm8 → в позицию AH
    AND  R4, R4, #0xFFFF00FF  // Обнуляем старший байт
    ORR  R4, R4, R0           // объединить AL и новый AH
    CPSID i                   // Запрет прерываний перед INT
    adr  r11, m00000008       // x86 00000006: CCh = int 10
    mrs  r12, apsr            // Сохраняем флаги
    push {r11, r12}           // Эмулируем PUSH IP, PUSH FLAGS
    ldr  r11, =0x11000040     // Адрес обработчика INT 10h
    ldr  r11, [r11]           // Адрес обработчика (IDT)
    mov  pc, r11              // Переход к обработчику
m00000008:
    MOV  R4, #0x4C00          // x86 00000008: B8h 00h 4Ch = MOV AX, 0x4C00
    CPSID i                   // Запрет прерываний перед INT
    adr  r11, m0000000D       // x86 0000000B: CCh = int 21
    mrs  r12, apsr            // Сохраняем флаги
    push {r11, r12}           // Эмулируем PUSH IP, PUSH FLAGS
    ldr  r11, =0x11000084     // Адрес обработчика INT 21h
    ldr  r11, [r11]           // Адрес обработчика (IDT)
    mov  pc, r11              // Переход к обработчику
m0000000D:
    NOP
    pop  {r0-r12, lr}         // Восстанавливаем сохранённые регистры
    bx   lr
