.syntax unified
.thumb

.global x86_iret
.type x86_iret, %function

x86_iret:
    POP   {r0, lr}             // r0 ← сохранённые флаги, LR ← адрес возврата
    MSR   APSR_nzcvq, r0       // восстановление флагов NZCVQ
    CPSIE i                    // разрешение прерываний (IF=1)
    BX    lr                   // возврат
