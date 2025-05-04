org 0x100

    call case0
    call newline
    call case1
    call newline
    call case2
    call newline
    call case3
    call newline
    call case4
    call newline
    call case5
    call newline

    mov ax, 0x4C00
    int 0x21

; -----------------------------
; Каждое caseN: устанавливает флаги, затем вызывает флаг-чек
; -----------------------------
case0:      ; ZF=1, PF=1, SF=0, CF=0, OF=0
    xor ax, ax      ; AL = 0 → ZF=1, PF=1, SF=0
    clc             ; CF=0
    call flags
    ret

case1:      ; CF=1, ZF=0, PF=?, SF=0
    stc             ; CF=1
    mov al, 1
    or al, 0        ; PF зависит от AL
    call flags
    ret

case2:      ; SF=1
    mov al, -1      ; AL = 0xFF → SF=1, PF=0
    and al, 0xFF
    call flags
    ret

case3:      ; OF=1
    mov al, 127     ; 0x7F
    add al, 1       ; AL = 0x80 → OF=1, SF=1
    call flags
    ret

case4:      ; ZF=0, CF=0, PF=1
    mov al, 0xAA    ; Чётное число битов → PF=1
    clc
    call flags
    ret

case5:      ; Ничего не установлено
    mov al, 1
    and al, 1       ; AL = 1 → ZF=0, PF=0, SF=0
    clc
    call flags
    ret

; -----------------------------
; Функция проверки флагов и печати
; -----------------------------
flags:
    jz print_z
after_z:
    jc print_c
after_c:
    jp print_p
after_p:
    js print_s
after_s:
    jo print_o
after_o:
    ret

; -----------------------------
; Метки вывода символов флагов
; -----------------------------
print_z: mov al, 'z'  ; ZF
    call print_char
    jmp after_z

print_c: mov al, 'c'  ; CF
    call print_char
    jmp after_c

print_p: mov al, 'p'  ; PF
    call print_char
    jmp after_p

print_s: mov al, 's'  ; SF
    call print_char
    jmp after_s

print_o: mov al, 'o'  ; OF
    call print_char
    jmp after_o

; -----------------------------
; Вывод символа AL на экран
; -----------------------------
print_char:
    mov ah, 0x0E
    int 0x10
    ret

newline:
    mov al, 13
    call print_char
    mov al, 10
    call print_char
    ret
