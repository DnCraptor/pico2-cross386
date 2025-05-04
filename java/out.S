start_it:
    // x86 00000000; opcode: E8; bytes: 3
    push {LR}
    bl   m00000029             // E8h 26h 00h = CALL +38 (m00000029)
    pop  {LR}
    // x86 00000003; opcode: E8; bytes: 3
    push {LR}
    bl   m0000008C             // E8h 86h 00h = CALL +134 (m0000008C)
    pop  {LR}
    // x86 00000006; opcode: E8; bytes: 3
    push {LR}
    bl   m00000030             // E8h 27h 00h = CALL +39 (m00000030)
    pop  {LR}
    // x86 00000009; opcode: E8; bytes: 3
    push {LR}
    bl   m0000008C             // E8h 80h 00h = CALL +128 (m0000008C)
    pop  {LR}
    // x86 0000000C; opcode: E8; bytes: 3
    push {LR}
    bl   m00000039             // E8h 2Ah 00h = CALL +42 (m00000039)
    pop  {LR}
    // x86 0000000F; opcode: E8; bytes: 3
    push {LR}
    bl   m0000008C             // E8h 7Ah 00h = CALL +122 (m0000008C)
    pop  {LR}
    // x86 00000012; opcode: E8; bytes: 3
    push {LR}
    bl   m00000041             // E8h 2Ch 00h = CALL +44 (m00000041)
    pop  {LR}
    // x86 00000015; opcode: E8; bytes: 3
    push {LR}
    bl   m0000008C             // E8h 74h 00h = CALL +116 (m0000008C)
    pop  {LR}
    // x86 00000018; opcode: E8; bytes: 3
    push {LR}
    bl   m00000049             // E8h 2Eh 00h = CALL +46 (m00000049)
    pop  {LR}
    // x86 0000001B; opcode: E8; bytes: 3
    push {LR}
    bl   m0000008C             // E8h 6Eh 00h = CALL +110 (m0000008C)
    pop  {LR}
    // x86 0000001E; opcode: E8; bytes: 3
    push {LR}
    bl   m00000050             // E8h 2Fh 00h = CALL +47 (m00000050)
    pop  {LR}
    // x86 00000021; opcode: E8; bytes: 3
    push {LR}
    bl   m0000008C             // E8h 68h 00h = CALL +104 (m0000008C)
    pop  {LR}
    // x86 00000024; opcode: B8; bytes: 3
    MOV  R4, #0x4C00          // B8h 00h 4Ch = MOV AX, 0x4C00
    // x86 00000027; opcode: CD; bytes: 2
    CPSID i                   // Запрет прерываний перед INT
    adr  r11, m00000029       // CCh = int 21
    mrs  r12, apsr            // Сохраняем флаги
    push {r11, r12}           // Эмулируем PUSH IP, PUSH FLAGS
    ldr  r11, =0x11000084     // Адрес обработчика INT 21h
    ldr  r11, [r11]           // Адрес обработчика (IDT)
    mov  pc, r11              // Переход к обработчику
m00000029:
    // x86 00000029; opcode: 31; bytes: 2
    // 01h C0h = XOR [Ew], R4
    MOV  R0, R4
    MOV  R1, R4
    MOV  R2, R0
    XORS R3, R0, R1
    MOV  R4, R3

    // x86 0000002B; opcode: F8; bytes: 1
    MOV  R0, #0
    LSRS R0, R0, #1    // CLC
    // x86 0000002C; opcode: E8; bytes: 3
    push {LR}
    bl   m00000059             // E8h 2Ah 00h = CALL +42 (m00000059)
    pop  {LR}
    // x86 0000002F; opcode: C3; bytes: 1
    BX LR                // C3h = RET
m00000030:
    // x86 00000030; opcode: F9; bytes: 1
    MOV  R0, #1
    LSRS R0, R0, #1    // STC
    // x86 00000031; opcode: B0; bytes: 2
    LDR  R0, =0x01            // B0h 01h = MOV AL, 0x01
    AND  R4, R4, #0xFFFFFF00  // 
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 00000033; opcode: 0C; bytes: 2
    MOV  R0, R4              // 04h 00h = OR  AL, (AL = R4)
    UXTB R0, R0              // оставляем только младший байт в R0, дальше используется как первый аргумент
    LDR  R1, =0            // загружаем константу в R1, дальше используется, как второй аргумент
    ORRS R2, R0, R1          // выполняем OR, в R2 9 бит результата
    UXTB R4, R2              // результат - только младший байт сохраняем в R4 (AL)
    // x86 00000035; opcode: E8; bytes: 3
    push {LR}
    bl   m00000059             // E8h 21h 00h = CALL +33 (m00000059)
    pop  {LR}
    // x86 00000038; opcode: C3; bytes: 1
    BX LR                // C3h = RET
m00000039:
    // x86 00000039; opcode: B0; bytes: 2
    LDR  R0, =0xFF            // B0h FFh = MOV AL, 0xFF
    AND  R4, R4, #0xFFFFFF00  // 
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 0000003B; opcode: 24; bytes: 2
    MOV  R0, R4              // 04h FFh = AND AL, (AL = R4)
    UXTB R0, R0              // оставляем только младший байт в R0, дальше используется как первый аргумент
    LDR  R1, =-1            // загружаем константу в R1, дальше используется, как второй аргумент
    ANDS R2, R0, R1          // выполняем AND, в R2 9 бит результата
    UXTB R4, R2              // результат - только младший байт сохраняем в R4 (AL)
    // x86 0000003D; opcode: E8; bytes: 3
    push {LR}
    bl   m00000059             // E8h 19h 00h = CALL +25 (m00000059)
    pop  {LR}
    // x86 00000040; opcode: C3; bytes: 1
    BX LR                // C3h = RET
m00000041:
    // x86 00000041; opcode: B0; bytes: 2
    LDR  R0, =0x7F            // B0h 7Fh = MOV AL, 0x7F
    AND  R4, R4, #0xFFFFFF00  // 
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 00000043; opcode: 04; bytes: 2
    MOV  R0, R4              // 04h 01h = ADD AL, (AL = R4)
    UXTB R0, R0              // оставляем только младший байт в R0, дальше используется как первый аргумент
    LDR  R1, =1            // загружаем константу в R1, дальше используется, как второй аргумент
    ADDS R2, R0, R1          // выполняем сложение, в R2 9 бит результата
    UXTB R4, R2              // результат - только младший байт сохраняем в R4 (AL)
    // x86 00000045; opcode: E8; bytes: 3
    push {LR}
    bl   m00000059             // E8h 11h 00h = CALL +17 (m00000059)
    pop  {LR}
    // x86 00000048; opcode: C3; bytes: 1
    BX LR                // C3h = RET
m00000049:
    // x86 00000049; opcode: B0; bytes: 2
    LDR  R0, =0xAA            // B0h AAh = MOV AL, 0xAA
    AND  R4, R4, #0xFFFFFF00  // 
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 0000004B; opcode: F8; bytes: 1
    MOV  R0, #0
    LSRS R0, R0, #1    // CLC
    // x86 0000004C; opcode: E8; bytes: 3
    push {LR}
    bl   m00000059             // E8h 0Ah 00h = CALL +10 (m00000059)
    pop  {LR}
    // x86 0000004F; opcode: C3; bytes: 1
    BX LR                // C3h = RET
m00000050:
    // x86 00000050; opcode: B0; bytes: 2
    LDR  R0, =0x01            // B0h 01h = MOV AL, 0x01
    AND  R4, R4, #0xFFFFFF00  // 
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 00000052; opcode: 24; bytes: 2
    MOV  R0, R4              // 04h 01h = AND AL, (AL = R4)
    UXTB R0, R0              // оставляем только младший байт в R0, дальше используется как первый аргумент
    LDR  R1, =1            // загружаем константу в R1, дальше используется, как второй аргумент
    ANDS R2, R0, R1          // выполняем AND, в R2 9 бит результата
    UXTB R4, R2              // результат - только младший байт сохраняем в R4 (AL)
    // x86 00000054; opcode: F8; bytes: 1
    MOV  R0, #0
    LSRS R0, R0, #1    // CLC
    // x86 00000055; opcode: E8; bytes: 3
    push {LR}
    bl   m00000059             // E8h 01h 00h = CALL +1 (m00000059)
    pop  {LR}
    // x86 00000058; opcode: C3; bytes: 1
    BX LR                // C3h = RET
m00000059:
    // x86 00000059; opcode: 74; bytes: 2
    BEQ m00000064            // 74h 09h = JZ +9 (m00000064)
m0000005B:
    // x86 0000005B; opcode: 72; bytes: 2
    BCS m0000006B            // 72h 0Eh = JC +14 (m0000006B)
m0000005D:
    // x86 0000005D; opcode: 7A; bytes: 2
    MRS  R1, APSR               // JP parity W/A
    LDRB R0, =X86_PF            // загрузить адрес переменной X86_PF
    LDRB R0, [R0]               // загрузить значение флага чётности
    CMP  R0, #1                 // JP, если PF == 1
    BEQ  m00000072_P                // 7Ah 13h = JP +19 (m00000072_P)
    MSR  APSR_nzcvq, R1         // восстановить только N,Z,C,V
m0000005F:
    // x86 0000005F; opcode: 78; bytes: 2
    BMI m00000079            // 78h 18h = JS +24 (m00000079)
m00000061:
    // x86 00000061; opcode: 70; bytes: 2
    BVS m00000080            // 70h 1Dh = JO +29 (m00000080)
m00000063:
    // x86 00000063; opcode: C3; bytes: 1
    BX LR                // C3h = RET
m00000064:
    // x86 00000064; opcode: B0; bytes: 2
    LDR  R0, =0x7A            // B0h 7Ah = MOV AL, 0x7A
    AND  R4, R4, #0xFFFFFF00  // 
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 00000066; opcode: E8; bytes: 3
    push {LR}
    bl   m00000087             // E8h 1Eh 00h = CALL +30 (m00000087)
    pop  {LR}
    // x86 00000069; opcode: EB; bytes: 2
    B m0000005B               // EBh F0h = JMP -16 (m0000005B)
m0000006B:
    // x86 0000006B; opcode: B0; bytes: 2
    LDR  R0, =0x63            // B0h 63h = MOV AL, 0x63
    AND  R4, R4, #0xFFFFFF00  // 
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 0000006D; opcode: E8; bytes: 3
    push {LR}
    bl   m00000087             // E8h 17h 00h = CALL +23 (m00000087)
    pop  {LR}
    // x86 00000070; opcode: EB; bytes: 2
    B m0000005D               // EBh EBh = JMP -21 (m0000005D)
m00000072_P:
    MSR APSR_nzcvq, R0        // восстановить только N,Z,C,V (после сохранения для PF)
m00000072:
    // x86 00000072; opcode: B0; bytes: 2
    LDR  R0, =0x70            // B0h 70h = MOV AL, 0x70
    AND  R4, R4, #0xFFFFFF00  // 
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 00000074; opcode: E8; bytes: 3
    push {LR}
    bl   m00000087             // E8h 10h 00h = CALL +16 (m00000087)
    pop  {LR}
    // x86 00000077; opcode: EB; bytes: 2
    B m0000005F               // EBh E6h = JMP -26 (m0000005F)
m00000079:
    // x86 00000079; opcode: B0; bytes: 2
    LDR  R0, =0x73            // B0h 73h = MOV AL, 0x73
    AND  R4, R4, #0xFFFFFF00  // 
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 0000007B; opcode: E8; bytes: 3
    push {LR}
    bl   m00000087             // E8h 09h 00h = CALL +9 (m00000087)
    pop  {LR}
    // x86 0000007E; opcode: EB; bytes: 2
    B m00000061               // EBh E1h = JMP -31 (m00000061)
m00000080:
    // x86 00000080; opcode: B0; bytes: 2
    LDR  R0, =0x6F            // B0h 6Fh = MOV AL, 0x6F
    AND  R4, R4, #0xFFFFFF00  // 
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 00000082; opcode: E8; bytes: 3
    push {LR}
    bl   m00000087             // E8h 02h 00h = CALL +2 (m00000087)
    pop  {LR}
    // x86 00000085; opcode: EB; bytes: 2
    B m00000063               // EBh DCh = JMP -36 (m00000063)
m00000087:
    // x86 00000087; opcode: B4; bytes: 2
    LDR  R0, =0x0E            // B4h 0Eh = MOV AH, 0x0E
    LSL  R0, R0, #8           // imm8 → в позицию AH
    AND  R4, R4, #0xFFFF00FF  // Обнуляем старший байт
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 00000089; opcode: CD; bytes: 2
    CPSID i                   // Запрет прерываний перед INT
    adr  r11, m0000008B       // CCh = int 10
    mrs  r12, apsr            // Сохраняем флаги
    push {r11, r12}           // Эмулируем PUSH IP, PUSH FLAGS
    ldr  r11, =0x11000040     // Адрес обработчика INT 10h
    ldr  r11, [r11]           // Адрес обработчика (IDT)
    mov  pc, r11              // Переход к обработчику
m0000008B:
    // x86 0000008B; opcode: C3; bytes: 1
    BX LR                // C3h = RET
m0000008C:
    // x86 0000008C; opcode: B0; bytes: 2
    LDR  R0, =0x0D            // B0h 0Dh = MOV AL, 0x0D
    AND  R4, R4, #0xFFFFFF00  // 
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 0000008E; opcode: E8; bytes: 3
    push {LR}
    bl   m00000087             // E8h F6h FFh = CALL -10 (m00000087)
    pop  {LR}
    // x86 00000091; opcode: B0; bytes: 2
    LDR  R0, =0x0A            // B0h 0Ah = MOV AL, 0x0A
    AND  R4, R4, #0xFFFFFF00  // 
    ORR  R4, R4, R0           // объединить AL и новый AH
    // x86 00000093; opcode: E8; bytes: 3
    push {LR}
    bl   m00000087             // E8h F1h FFh = CALL -15 (m00000087)
    pop  {LR}
    // x86 00000096; opcode: C3; bytes: 1
    BX LR                // C3h = RET
    // x86 00000097; opcode: 90; bytes: 1
// 00000097: 90h
