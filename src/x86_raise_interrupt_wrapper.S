.syntax unified
.thumb

.global x86_raise_interrupt_wrapper
.type x86_raise_interrupt_wrapper, %function

x86_raise_interrupt_wrapper:
    push {r0-r12, lr}         // Сохраняем рабочие регистры (r1-r12, lr)
#include "x86_raise_interrupt.h"
    pop  {r0-r12, lr}         // Восстанавливаем сохранённые регистры
    bx   lr
