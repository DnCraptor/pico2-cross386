.syntax unified
.thumb

.global x86_int16_wrapper
.type x86_int16_wrapper, %function

x86_int16_wrapper:
    push {r1-r12, lr}         // Сохраняем рабочие регистры (r1-r12, lr)
#include "x86_int16.h"
    mov  r0, r4               // В r0 результат (из r4)
    pop  {r1-r12, lr}         // Восстанавливаем сохранённые регистры
    bx   lr
