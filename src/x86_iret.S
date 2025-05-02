.syntax unified
.thumb

.global x86_iret
.type x86_iret, %function

x86_iret:
    POP   {r0, r1}             // r1 ← сохранённые флаги, r0 ← адрес возврата
    MSR   APSR_nzcvq, r1       // восстановление флагов NZCVQ
    CPSIE i                    // разрешение прерываний (IF=1)
    MOV   pc, r0               // возврат
