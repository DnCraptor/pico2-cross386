    // naked (withot gcc-wrapper)
    CPSID i                   // Запрет прерываний
    adr  r11, 1f              // Адрес возврата (метка 1)
    mrs  r12, apsr            // Сохраняем флаги
    push {r11, r12}           // Эмулируем PUSH IP, PUSH FLAGS
    ldr  r11, [r0]            // Адрес обработчика (IDT)
    mov  pc, r11              // Переход к обработчику
1:                            // Метка возврата
