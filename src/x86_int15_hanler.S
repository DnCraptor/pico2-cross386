.syntax unified
.thumb

.global x86_int15_hanler
.type x86_int15_hanler, %function

x86_int15_hanler:
    MOV   r0, r4               // Передаем значение из r4 (EAX) в r0 (первый аргумент для функции C)
    MOV   r1, r5               // Передаем значение из r5 (EBX) в r1
    MOV   r2, r6               // Передаем значение из r6 (ECX) в r2
    MOV   r3, r7               // Передаем значение из r7 (EDX) в r3
    BL    x86_int15_hanler_C   // Это имя будет связано с C функцией на этапе линковки
    MOV   r4, r0               // result in r0

    AND   r0, r0, #(1 << 29)   // CF flag is part of the result
    CMP   r0, #0               // Сравниваем результат с 0 (0 - успешно)
    BNE   1f                   // Если r0 != 0, установить CF (ошибка)

    POP   {r0, r1}             // r1 ← сохранённые флаги, r0 ← адрес возврата
    BIC   r1, r1, #(1 << 29)   // r0 == 0 → сбросить CF (бит 29 в APSR)
    B     2f
1:
    POP   {r0, r1}             // r1 ← сохранённые флаги, r0 ← адрес возврата
    ORR   r1, r1, #(1 << 29)   // r0 != 0 → установить CF
2:
    MSR   APSR_nzcvq, r1       // восстановление флагов NZCVQ
    CPSIE i                    // разрешение прерываний (IF=1)
    MOV   pc, r0               // возврат
