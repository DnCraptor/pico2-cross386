.syntax unified
.thumb

.global x86_int16_hanler
.type x86_int16_hanler, %function

x86_int16_hanler:
    MOV   r0, r4               // Передаем значение из r4 (EAX) в r0 (первый аргумент для функции C)
    MOV   r1, r5               // Передаем значение из r5 (EBX) в r1
    MOV   r2, r6               // Передаем значение из r6 (ECX) в r2
    MOV   r3, r7               // Передаем значение из r7 (EDX) в r3
    BL    x86_int16_hanler_C   // Это имя будет связано с C функцией на этапе линковки
    MOV   r4, r0               // result in r0 -> r4 (EAX)
    MOV   r3, r0               // result in r0 -> r3 для работы с флагами

    AND   r3, r3, #0x60000000  // CF (29) & ZF (30) flags are part of the result
    POP   {r0, r1}             // r1 ← сохранённые флаги, r3 ← адрес возврата

    BIC   r1, r1, #0x60000000  // сбросить флаги CF (29) & ZF (30)
    ORR   r1, r1, r3           // установить флаги CF (29) & ZF (30) на основе результата
    MSR   APSR_nzcvq, r1       // восстановление флагов NZCVQ с учётом изменений
    CPSIE i                    // разрешение прерываний (IF=1)
    MOV   pc, r0               // возврат
