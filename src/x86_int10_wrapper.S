.syntax unified
.thumb

.global x86_int10_wrapper
.type x86_int10_wrapper, %function

x86_int10_wrapper:
    push {r1-r12, lr}         // Сохраняем рабочие регистры (r1-r12, lr)
#include "x86_int10.h"
    mov  r0, r4               // В r0 результат (из r4)
    pop  {r1-r12, lr}         // Восстанавливаем сохранённые регистры
    bx   lr
