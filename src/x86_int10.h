    // naked int 10h call (withot gcc-wrapper)
    mov  r4, r0               // r4 = EAX
    mov  r5, r1               // r5 = EBX
    mov  r6, r2               // r6 = ECX
    mov  r7, r3               // r7 = EDX
    CPSID i                   // Запрет прерываний
    adr  r11, 1f              // Адрес возврата (метка 1)
    mrs  r12, apsr            // Сохраняем флаги
    push {r11, r12}           // Эмулируем PUSH IP, PUSH FLAGS
    ldr  r11, =0x11000040     // Адрес обработчика INT 10h
    ldr  r11, [r11]
    mov  pc, r11              // Переход к обработчику
1:                            // Метка возврата
