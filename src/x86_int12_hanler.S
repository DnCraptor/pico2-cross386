.syntax unified
.thumb

.global x86_int12_hanler
.type x86_int12_hanler, %function

x86_int12_hanler:
    LDR   r4, =640             // result in AX, eax is r4 on m33

    POP   {r0, r1}             // r1 ← сохранённые флаги, r0 ← адрес возврата
    MSR   APSR_nzcvq, r1       // восстановление флагов NZCVQ
    CPSIE i                    // разрешение прерываний (IF=1)
    MOV   pc, r0               // возврат
