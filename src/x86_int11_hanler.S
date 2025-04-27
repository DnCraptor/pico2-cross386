.syntax unified
.thumb

.global x86_int11_hanler
.type x86_int11_hanler, %function

x86_int11_hanler:
    BL    x86_int11_hanler_C   // Это имя будет связано с C функцией на этапе линковки
    MOV   r4, r0               // result in r0

    POP   {r0, r1}             // r1 ← сохранённые флаги, r0 ← адрес возврата
    MSR   APSR_nzcvq, r1       // восстановление флагов NZCVQ
    CPSIE i                    // разрешение прерываний (IF=1)
    MOV   pc, r0               // возврат
