    // naked (withot gcc-wrapper)
    CPSID i                   // Запрет прерываний
    ldr  r11, =1f             // Адрес возврата (метка 1)
    orrs r11, r11, #1         // Установить bit 0, чтобы указать Thumb-режим
    mrs  r12, apsr            // Сохраняем флаги
    push {r11, r12}           // Эмулируем PUSH IP, PUSH FLAGS
    ldr  r11, [r0]            // Адрес обработчика (IDT)
    mov  pc, r11              // Переход к обработчику
1:                            // Метка возврата
