import java.util.ArrayList;
import java.util.List;

public class M33Replacement {

    final int offset;
    final int keyByte;
    String m33candidate;
    boolean data = false; // it is used as data in traced flow
    boolean passed = false; // it is code traced
    int pointsTo = -1; // may jumps to some block
    int nextOffset = -1;
    int bytes = 1;
    boolean CFrequired = false;
    boolean PFrequired = false;
    boolean AFrequired = false;
    boolean CFback = false;
    boolean PFback = false;
    boolean AFback = false;
    boolean changesCF = false;
    boolean changesPF = false;
    boolean changesAF = false;
    boolean need_recover = false; // after PF check
    final List<Integer> pointedFrom = new ArrayList<>();

    public M33Replacement(final int offset, int keyByte, String m33candidate) {
        this.offset = offset;
        this.keyByte = keyByte;
        this.m33candidate = m33candidate;
        this.passed = true;
        this.nextOffset = -1; // ??
    }

    private String emitPF8(String resultReg) {
        return  "    // вычислить флаг чётности и сохранить в X86_PF\n" +
                "    MOV  R12, " + resultReg + "             // результат после ADD (8 бит)\n" +
                "    EOR  R12, R12, R12, LSR #4              // XOR со сдвигом на 4 бита\n" +
                "    EOR  R12, R12, R12, LSR #2              // XOR со сдвигом на 2 бита\n" +
                "    EOR  R12, R12, R12, LSR #1              // XOR со сдвигом на 1 бит\n" +
                "    MVN  R12, R12                           // инвертировать результат\n" +
                "    ANDS R12, R12, #1                       // чётность = ~XOR(all bits) & 1\n" +
                "    ADR  R11, =X86_PF                       // загрузить адрес переменной\n" +
                "    STRB R12, [R11]                         // уложить PF в переменную\n";
    }

    private String emitAF(String reg1, String reg2, String resultReg) {
        return  "    // вычислить флаг AF и сохранить в X86_AF\n" +
                "    EOR  R12, " + reg1 + ", " + reg2 + "       // R12 = reg1 ^ reg2\n" +
                "    EOR  R12, R12, " + resultReg + "           // R12 = R12 ^ result\n" +
                "    LSR  R12, R12, #4                          // сдвиг на 4: бит 4 в бит 0\n" +
                "    ANDS R12, R12, #1                          // изолировать бит 0\n" +
                "    ADR  R11, =X86_AF                          // загрузить адрес переменной\n" +
                "    STRB R12, [R11]                            // сохранить AF\n";
    }

    private String emitCF8(String resultReg) {
        return  "    MOV   R11, " + resultReg + "                 // скопировать результат\n" +
                "    MRS   R12, APSR                      // сохранить флаги\n" +
                "    CMP   R11, #0\n" +
                "    IT    LT\n" +
                "    RSBLT R11, R11, #0                     // модуль\n" +
                "    MSR   APSR_nzcvq, R12                // восстановить флаги\n" +
                "    LSL   R11, R11, #24                    // флаг переноса = бит 8";
    }

    private String emitCF16(String resultReg) {
        return  "    MOV   R11, " + resultReg + "                 // скопировать результат\n" +
                "    MRS   R12, APSR                      // сохранить флаги\n" +
                "    CMP   R11, #0\n" +
                "    IT    LT\n" +
                "    RSBLT R11, R11, #0                     // модуль\n" +
                "    MSR   APSR_nzcvq, R12                // восстановить флаги\n" +
                "    LSL   R11, R11, #16                    // флаг переноса = бит 16";
    }

    public M33Replacement(final int offset, final int prevOffset, final ArrayList<Byte> bin) {
        this.offset = offset;
        this.pointedFrom.add(prevOffset);
        this.nextOffset = offset + 1;
        this.keyByte = bin.get(offset) & 0xff;
        regenerate(bin);
    }

    public void regenerate(final ArrayList<Byte> bin) {
        switch(keyByte) {
            case 0x00: { // ADD Eb, Gb
                changesCF = true;
                changesPF = true;
                changesAF = true;
                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg8 = {"AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"};
                final String[] armReg8 = {"R4", "R6", "R7", "R5", "R4", "R6", "R7", "R5"};
                final boolean[] isHigh = {false, false, false, false, true, true, true, true};

                final String srcReg = armReg8[reg];
                final boolean srcHigh = isHigh[reg];

                final String[] rmExprs = {
                        "ADD  R0, R5, R8           // [BX+SI]<-",
                        "ADD  R0, R5, R9           // [BX+DI]<-",
                        "ADD  R0, R10, R8          // [BP+SI]<-",
                        "ADD  R0, R10, R9          // [BP+DI]<-",
                        "MOV  R0, R8               // [SI]<-",
                        "MOV  R0, R9               // [DI]<-",
                        "MOV  R0, R10              // [BP]<-",
                        "MOV  R0, R5               // [BX]<-"
                };

                final String comment = String.format("    // 00h %02Xh = ADD [Eb], %s", modrm, x86Reg8[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRB R3, [R0]\n" +
                            (srcHigh ?
                                    "    LSRS R2, " + srcReg + ", #8\n" :
                                    "    UXTB R2, " + srcReg + "\n") +
                            "    MOV  R1, R2\n" +         // R1 = src
                            "    MOV  R2, R3\n" +         // R2 = dst
                            "    ADDS R3, R3, R1\n" +     // R3 = dst + src
                            "    STRB R3, [R0]\n" +
                            "    MOV  R0, R2\n" +         // R0 = old dst
                            "    MOV  R2, R3\n";          // R2 = result
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + String.format("\n%s\n    ADDS R0, R0, #%d\n", rmExprs[rm], disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRB R3, [R0]\n" +
                            (srcHigh ?
                                    "    LSRS R2, " + srcReg + ", #8\n" :
                                    "    UXTB R2, " + srcReg + "\n") +
                            "    MOV  R1, R2\n" +
                            "    MOV  R2, R3\n" +
                            "    ADDS R3, R3, R1\n" +
                            "    STRB R3, [R0]\n" +
                            "    MOV  R0, R2\n" +
                            "    MOV  R2, R3\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + String.format("\n%s\n    ADD  R0, R0, #%d\n", rmExprs[rm], disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRB R3, [R0]\n" +
                            (srcHigh ?
                                    "    LSRS R2, " + srcReg + ", #8\n" :
                                    "    UXTB R2, " + srcReg + "\n") +
                            "    MOV  R1, R2\n" +
                            "    MOV  R2, R3\n" +
                            "    ADDS R3, R3, R1\n" +
                            "    STRB R3, [R0]\n" +
                            "    MOV  R0, R2\n" +
                            "    MOV  R2, R3\n";
                    bytes = 4;
                } else { /// if (mod == 0b11)
                    final String[] arm8regs = {"R4", "R6", "R7", "R5", "R4", "R6", "R7", "R5"};
                    final boolean dstHigh = isHigh[rm];
                    final String dstReg = arm8regs[rm];

                    m33candidate = comment + "\n" +
                            (dstHigh ?
                                    "    LSRS R0, " + dstReg + ", #8\n" :
                                    "    UXTB R0, " + dstReg + "\n") +
                            (srcHigh ?
                                    "    LSRS R1, " + srcReg + ", #8\n" :
                                    "    UXTB R1, " + srcReg + "\n") +
                            "    ADDS R2, R0, R1\n" +
                            (dstHigh ?
                                    "    LSL  R2, R2, #8\n" +
                                            "    BIC  " + dstReg + ", " + dstReg + ", #0xFF00\n" +
                                            "    ORR  " + dstReg + ", " + dstReg + ", R2\n" :
                                    "    BIC  " + dstReg + ", " + dstReg + ", #0xFF\n" +
                                            "    ORR  " + dstReg + ", " + dstReg + ", R2\n");
                    bytes = 2;
                }

                // Обработка флагов
                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R2");
                }
                if (this.AFrequired) {
                    m33candidate += "\n" + emitAF("R0", "R1", "R2");
                }
                if (this.CFrequired) {
                    // Используем R2 как результат до обрезки
                    m33candidate += "\n" + emitCF8("R2");
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0x01: { // ADD Ew, Gw
                // AF, PF, CF эмулируются вручную
                changesCF = true;
                changesPF = true;
                changesAF = true;

                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg16 = {"R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11"};
                final String srcReg = x86Reg16[reg];

                final String[] rmExprs = {
                        "ADD  R0, R5, R8           // [BX+SI]",
                        "ADD  R0, R5, R9           // [BX+DI]",
                        "ADD  R0, R10, R8          // [BP+SI]",
                        "ADD  R0, R10, R9          // [BP+DI]",
                        "MOV  R0, R8               // [SI]",
                        "MOV  R0, R9               // [DI]",
                        "MOV  R0, R10              // [BP]",
                        "MOV  R0, R5               // [BX]"
                };

                final String comment = String.format("    // 01h %02Xh = ADD [Ew], %s", modrm, x86Reg16[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R3           // old value\n" +
                            "    ADDS R3, R3, R1       // result in R3, flags auto-updated\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ADDS R0, R0, #%d\n", disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ADDS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ADD  R0, R0, #%d\n", disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ADDS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 4;
                } else { // mod == 0b11, регистровый режим
                    final String dstReg = x86Reg16[rm];
                    m33candidate = comment + "\n" +
                            "    MOV  R0, " + dstReg + "\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R0\n" +
                            "    ADDS R3, R0, R1\n" +
                            "    MOV  " + dstReg + ", R3\n";
                    bytes = 2;
                }

                // Ручная эмуляция флагов AF, PF, CF
                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R3");
                }
                if (this.AFrequired) {
                    m33candidate += "\n" + emitAF("R0", "R1", "R3");
                }
                if (this.CFrequired) {
                    m33candidate += "\n" + emitCF16("R3");
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x02: { // ADD Gw, Ew
                // AF, PF, CF эмулируются вручную
                changesCF = true;
                changesPF = true;
                changesAF = true;

                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg16 = {"R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11"};
                final String dstReg = x86Reg16[reg];

                final String[] rmExprs = {
                        "ADD  R0, R5, R8           // [BX+SI]",
                        "ADD  R0, R5, R9           // [BX+DI]",
                        "ADD  R0, R10, R8          // [BP+SI]",
                        "ADD  R0, R10, R9          // [BP+DI]",
                        "MOV  R0, R8               // [SI]",
                        "MOV  R0, R9               // [DI]",
                        "MOV  R0, R10              // [BP]",
                        "MOV  R0, R5               // [BX]"
                };

                final String comment = String.format("    // 02h %02Xh = ADD %s, [Ew]", modrm, x86Reg16[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3           // old value\n" +
                            "    ADDS R3, R3, R1       // result in R3, flags auto-updated\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ADDS R0, R0, #%d\n", disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ADDS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ADD  R0, R0, #%d\n", disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ADDS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 4;
                } else { // mod == 0b11, регистровый режим
                    final String srcReg = x86Reg16[rm];
                    m33candidate = comment + "\n" +
                            "    MOV  R0, " + srcReg + "\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R0\n" +
                            "    ADDS R3, R0, R1\n" +
                            "    MOV  " + dstReg + ", R3\n";
                    bytes = 2;
                }

                // Ручная эмуляция флагов AF, PF, CF
                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R3");
                }
                if (this.AFrequired) {
                    m33candidate += "\n" + emitAF("R0", "R1", "R3");
                }
                if (this.CFrequired) {
                    // Используем R3 как результат
                    m33candidate += "\n" + emitCF16("R3");
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x03: { // ADD Gw, Ew (с адресованием через память)
                // AF, PF, CF эмулируются вручную
                changesCF = true;
                changesPF = true;
                changesAF = true;

                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg16 = {"R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11"};
                final String dstReg = x86Reg16[reg];

                final String[] rmExprs = {
                        "ADD  R0, R5, R8           // [BX+SI]",
                        "ADD  R0, R5, R9           // [BX+DI]",
                        "ADD  R0, R10, R8          // [BP+SI]",
                        "ADD  R0, R10, R9          // [BP+DI]",
                        "MOV  R0, R8               // [SI]",
                        "MOV  R0, R9               // [DI]",
                        "MOV  R0, R10              // [BP]",
                        "MOV  R0, R5               // [BX]"
                };

                final String comment = String.format("    // 03h %02Xh = ADD %s, [Ew]", modrm, x86Reg16[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3           // old value\n" +
                            "    ADDS R3, R3, R1       // result in R3, flags auto-updated\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ADDS R0, R0, #%d\n", disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ADDS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ADD  R0, R0, #%d\n", disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ADDS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 4;
                } else { // mod == 0b11, регистровый режим
                    final String srcReg = x86Reg16[rm];
                    m33candidate = comment + "\n" +
                            "    MOV  R0, " + srcReg + "\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R0\n" +
                            "    ADDS R3, R0, R1\n" +
                            "    MOV  " + dstReg + ", R3\n";
                    bytes = 2;
                }

                // Ручная эмуляция флагов AF, PF, CF
                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R3");
                }
                if (this.AFrequired) {
                    m33candidate += "\n" + emitAF("R0", "R1", "R3");
                }
                if (this.CFrequired) {
                    // Используем R3 как результат
                    m33candidate += "\n" + emitCF16("R3");
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x04: { // ADD AL, Ib
                changesCF = true;
                changesPF = true;
                changesAF = true;

                final int imm8 = bin.get(offset + 1); // 8-битное значение, которое прибавляем к AL (R4)

                m33candidate = String.format("" +
                                "    MOV  R0, R4              // 04h %02Xh = ADD AL, (AL = R4)\n" +
                                "    UXTB R0, R0              // оставляем только младший байт в R0, дальше используется как первый аргумент\n" +
                                "    LDR  R1, =%d            // загружаем константу в R1, дальше используется, как второй аргумент\n" +
                                "    ADDS R2, R0, R1          // выполняем сложение, в R2 9 бит результата\n" +
                                "    UXTB R4, R2              // результат - только младший байт сохраняем в R4 (AL)",
                        imm8 & 0xFF, imm8
                );

                bytes = 2;
                nextOffset = offset + bytes;

                // Вычисление флага чётности (PF)
                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R2");
                }
                // Вычисление флага BCD (AF)
                if (this.AFrequired) {
                    m33candidate += "\n" + emitAF("R0", "R1", "R2");
                }
                // Учет флага переноса (CF)
                if (this.CFrequired) {
                    // Используем R2 как результат до обрезки
                    m33candidate += "\n" + emitCF8("R2");
                }
                break;
            }
            case 0x05: { // ADD AX, Imm16 — в 16-битном режиме
                changesCF = true;
                changesPF = true;
                changesAF = true;

                final int imm16 = (bin.get(offset + 1) & 0xFF)
                        | ((bin.get(offset + 2) & 0xFF) << 8);

                m33candidate = String.format(
                        "    MOV   R0, R4              // 05h %04Xh = ADD AX, 0x%04X (AX = R4 & 0xFFFF)\n" +
                        "    UXTH  R0, R0              // оставить только 16 бит (AX)\n" +
                        "    LDR   R1, =%d             // immediate 16-бит\n" +
                        "    ADDS  R2, R0, R1          // R2 = AX + imm16\n" +
                        "    UXTH  R4, R2              // сохранить только младшие 16 бит (AX) обратно в R4\n",
                        imm16 & 0xFFFF, imm16
                );

                bytes = 3;
                nextOffset = offset + bytes;

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R2"); // we should use 8-bit parity for 16-bit op (historically)
                }
                if (this.AFrequired) {
                    m33candidate += "\n" + emitAF("R0", "R1", "R2");
                }
                if (this.CFrequired) {
                    m33candidate += "\n" + emitCF16("R2");
                }

                break;
            }
            case 0x06: { // PUSH ES
                m33candidate =  "    LDR  R0, =X86_ES          // 06h = PUSH ES\n" +
                        "    LDRH R1, [R0]             // загрузить значение сегмента (16 бит) \n" +
                        "    PUSH {R1}";               // сохраняем сегмент ES в стек
                break;
            }
            case 0x07: { // POP ES
                m33candidate =  "    POP  {R1}                // 07h = POP ES\n" +
                        "    LDR  R0, =X86_ES          // загрузка адреса для сегмента ES\n" +
                        "    STRH R1, [R0]             // сохраняем значение из R1 в сегмент ES (16 бит)";
                break;
            }
            case 0x08: { // OR Eb, Gb
                changesPF = true;
                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg8 = {"AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"};
                final String[] armReg8 = {"R4", "R6", "R7", "R5", "R4", "R6", "R7", "R5"};
                final boolean[] isHigh = {false, false, false, false, true, true, true, true};

                final String srcReg = armReg8[reg];
                final boolean srcHigh = isHigh[reg];

                final String[] rmExprs = {
                        "ORR  R0, R5, R8           // [BX+SI]<-",
                        "ORR  R0, R5, R9           // [BX+DI]<-",
                        "ORR  R0, R10, R8          // [BP+SI]<-",
                        "ORR  R0, R10, R9          // [BP+DI]<-",
                        "MOV  R0, R8               // [SI]<-",
                        "MOV  R0, R9               // [DI]<-",
                        "MOV  R0, R10              // [BP]<-",
                        "MOV  R0, R5               // [BX]<-"
                };

                final String comment = String.format("    // 00h %02Xh = OR  [Eb], %s", modrm, x86Reg8[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRB R3, [R0]\n" +
                            (srcHigh ?
                                    "    LSRS R2, " + srcReg + ", #8\n" :
                                    "    UXTB R2, " + srcReg + "\n") +
                            "    MOV  R1, R2\n" +         // R1 = src
                            "    MOV  R2, R3\n" +         // R2 = dst
                            "    ORRS R3, R3, R1\n" +     // R3 = dst | src
                            "    STRB R3, [R0]\n" +
                            "    MOV  R0, R2\n" +         // R0 = old dst
                            "    MOV  R2, R3\n";          // R2 = result
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + String.format("\n%s\n    ORRS R0, R0, #%d\n", rmExprs[rm], disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRB R3, [R0]\n" +
                            (srcHigh ?
                                    "    LSRS R2, " + srcReg + ", #8\n" :
                                    "    UXTB R2, " + srcReg + "\n") +
                            "    MOV  R1, R2\n" +
                            "    MOV  R2, R3\n" +
                            "    ORRS R3, R3, R1\n" +
                            "    STRB R3, [R0]\n" +
                            "    MOV  R0, R2\n" +
                            "    MOV  R2, R3\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + String.format("\n%s\n    ORR  R0, R0, #%d\n", rmExprs[rm], disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRB R3, [R0]\n" +
                            (srcHigh ?
                                    "    LSRS R2, " + srcReg + ", #8\n" :
                                    "    UXTB R2, " + srcReg + "\n") +
                            "    MOV  R1, R2\n" +
                            "    MOV  R2, R3\n" +
                            "    ORRS R3, R3, R1\n" +
                            "    STRB R3, [R0]\n" +
                            "    MOV  R0, R2\n" +
                            "    MOV  R2, R3\n";
                    bytes = 4;
                } else { /// if (mod == 0b11)
                    final String[] arm8regs = {"R4", "R6", "R7", "R5", "R4", "R6", "R7", "R5"};
                    final boolean dstHigh = isHigh[rm];
                    final String dstReg = arm8regs[rm];

                    m33candidate = comment + "\n" +
                            (dstHigh ?
                                    "    LSRS R0, " + dstReg + ", #8\n" :
                                    "    UXTB R0, " + dstReg + "\n") +
                            (srcHigh ?
                                    "    LSRS R1, " + srcReg + ", #8\n" :
                                    "    UXTB R1, " + srcReg + "\n") +
                            "    ORRS R2, R0, R1\n" +
                            (dstHigh ?
                                    "    LSL  R2, R2, #8\n" +
                                            "    BIC  " + dstReg + ", " + dstReg + ", #0xFF00\n" +
                                            "    ORR  " + dstReg + ", " + dstReg + ", R2\n" :
                                    "    BIC  " + dstReg + ", " + dstReg + ", #0xFF\n" +
                                            "    ORR  " + dstReg + ", " + dstReg + ", R2\n");
                    bytes = 2;
                }

                // Обработка флагов
                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R2");
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0x09: { // OR  Ew, Gw
                changesPF = true;

                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg16 = {"R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11"};
                final String srcReg = x86Reg16[reg];

                final String[] rmExprs = {
                        "ORR  R0, R5, R8           // [BX+SI]",
                        "ORR  R0, R5, R9           // [BX+DI]",
                        "ORR  R0, R10, R8          // [BP+SI]",
                        "ORR  R0, R10, R9          // [BP+DI]",
                        "MOV  R0, R8               // [SI]",
                        "MOV  R0, R9               // [DI]",
                        "MOV  R0, R10              // [BP]",
                        "MOV  R0, R5               // [BX]"
                };

                final String comment = String.format("    // 01h %02Xh = ORR [Ew], %s", modrm, x86Reg16[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R3           // old value\n" +
                            "    ORRS R3, R3, R1       // result in R3, flags auto-updated\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ORRS R0, R0, #%d\n", disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ORRS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ORR  R0, R0, #%d\n", disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ORRS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 4;
                } else { // mod == 0b11, регистровый режим
                    final String dstReg = x86Reg16[rm];
                    m33candidate = comment + "\n" +
                            "    MOV  R0, " + dstReg + "\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R0\n" +
                            "    ORRS R3, R0, R1\n" +
                            "    MOV  " + dstReg + ", R3\n";
                    bytes = 2;
                }

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R3");
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x0A: { // OR  Gw, Ew
                changesPF = true;

                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg16 = {"R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11"};
                final String dstReg = x86Reg16[reg];

                final String[] rmExprs = {
                        "ORR  R0, R5, R8           // [BX+SI]",
                        "ORR  R0, R5, R9           // [BX+DI]",
                        "ORR  R0, R10, R8          // [BP+SI]",
                        "ORR  R0, R10, R9          // [BP+DI]",
                        "MOV  R0, R8               // [SI]",
                        "MOV  R0, R9               // [DI]",
                        "MOV  R0, R10              // [BP]",
                        "MOV  R0, R5               // [BX]"
                };

                final String comment = String.format("    // 02h %02Xh = ORR %s, [Ew]", modrm, x86Reg16[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3           // old value\n" +
                            "    ORRS R3, R3, R1       // result in R3, flags auto-updated\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ORRS R0, R0, #%d\n", disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ORRS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ORR  R0, R0, #%d\n", disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ORRS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 4;
                } else { // mod == 0b11, регистровый режим
                    final String srcReg = x86Reg16[rm];
                    m33candidate = comment + "\n" +
                            "    MOV  R0, " + srcReg + "\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R0\n" +
                            "    ORRS R3, R0, R1\n" +
                            "    MOV  " + dstReg + ", R3\n";
                    bytes = 2;
                }

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R3");
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x0B: { // OR  Gw, Ew (с адресованием через память)
                changesPF = true;

                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg16 = {"R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11"};
                final String dstReg = x86Reg16[reg];

                final String[] rmExprs = {
                        "ORR  R0, R5, R8           // [BX+SI]",
                        "ORR  R0, R5, R9           // [BX+DI]",
                        "ORR  R0, R10, R8          // [BP+SI]",
                        "ORR  R0, R10, R9          // [BP+DI]",
                        "MOV  R0, R8               // [SI]",
                        "MOV  R0, R9               // [DI]",
                        "MOV  R0, R10              // [BP]",
                        "MOV  R0, R5               // [BX]"
                };

                final String comment = String.format("    // 03h %02Xh = ORR %s, [Ew]", modrm, x86Reg16[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3           // old value\n" +
                            "    ORRS R3, R3, R1       // result in R3, flags auto-updated\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ORRS R0, R0, #%d\n", disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ORRS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ORR  R0, R0, #%d\n", disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ORRS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 4;
                } else { // mod == 0b11, регистровый режим
                    final String srcReg = x86Reg16[rm];
                    m33candidate = comment + "\n" +
                            "    MOV  R0, " + srcReg + "\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R0\n" +
                            "    ORRS R3, R0, R1\n" +
                            "    MOV  " + dstReg + ", R3\n";
                    bytes = 2;
                }

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R3");
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x0C: { // OR  AL, Ib
                changesPF = true;

                final int imm8 = bin.get(offset + 1); // 8-битное значение, которое прибавляем к AL (R4)

                m33candidate = String.format("" +
                                "    MOV  R0, R4              // 04h %02Xh = OR  AL, (AL = R4)\n" +
                                "    UXTB R0, R0              // оставляем только младший байт в R0, дальше используется как первый аргумент\n" +
                                "    LDR  R1, =%d            // загружаем константу в R1, дальше используется, как второй аргумент\n" +
                                "    ORRS R2, R0, R1          // выполняем OR, в R2 9 бит результата\n" +
                                "    UXTB R4, R2              // результат - только младший байт сохраняем в R4 (AL)",
                        imm8 & 0xFF, imm8
                );

                bytes = 2;
                nextOffset = offset + bytes;

                // Вычисление флага чётности (PF)
                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R2");
                }
                break;
            }
            case 0x0D: { // OR  AX, Imm16 — в 16-битном режиме
                changesCF = true;
                changesPF = true;
                changesAF = true;

                final int imm16 = (bin.get(offset + 1) & 0xFF)
                        | ((bin.get(offset + 2) & 0xFF) << 8);

                m33candidate = String.format(
                        "    MOV   R0, R4              // 05h %04Xh = OR  AX, 0x%04X (AX = R4 & 0xFFFF)\n" +
                                "    UXTH  R0, R0              // оставить только 16 бит (AX)\n" +
                                "    LDR   R1, =%d             // immediate 16-бит\n" +
                                "    ORRS  R2, R0, R1          // R2 = AX + imm16\n" +
                                "    UXTH  R4, R2              // сохранить только младшие 16 бит (AX) обратно в R4\n",
                        imm16 & 0xFFFF, imm16 & 0xFFFF, imm16
                );

                bytes = 3;
                nextOffset = offset + bytes;

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R2"); // we should use 8-bit parity for 16-bit op (historically)
                }

                break;
            }
            case 0x0E: // PUSH CS
                m33candidate =  "    LDR  R0, =X86_CS          // 0Eh = PUSH CS\n" +
                                "    LDRH R1, [R0]             // загрузить значение сегмента (16 бит) \n" +
                                "    PUSH {R1}";
                break;
            case 0x1E: // PUSH DS
                m33candidate =  "    LDR  R0, =X86_DS          // 0Eh = PUSH DS\n" +
                                "    LDRH R1, [R0]             // загрузить значение сегмента (16 бит) \n" +
                                "    PUSH {R1}";
                break;
            case 0x1F: // POP DS
                m33candidate =  "    POP  {R0}                  // 1Fh = POP DS\n" +
                                "    LDR  R1, =X86_DS           // адрес глобальной переменной \n" +
                                "    STRH R0, [R1]              // записать в неё 16 бит";
                break;
            case 0x20: { // AND Eb, Gb
                changesPF = true;
                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg8 = {"AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"};
                final String[] armReg8 = {"R4", "R6", "R7", "R5", "R4", "R6", "R7", "R5"};
                final boolean[] isHigh = {false, false, false, false, true, true, true, true};

                final String srcReg = armReg8[reg];
                final boolean srcHigh = isHigh[reg];

                final String[] rmExprs = {
                        "AND  R0, R5, R8           // [BX+SI]<-",
                        "AND  R0, R5, R9           // [BX+DI]<-",
                        "AND  R0, R10, R8          // [BP+SI]<-",
                        "AND  R0, R10, R9          // [BP+DI]<-",
                        "MOV  R0, R8               // [SI]<-",
                        "MOV  R0, R9               // [DI]<-",
                        "MOV  R0, R10              // [BP]<-",
                        "MOV  R0, R5               // [BX]<-"
                };

                final String comment = String.format("    // 00h %02Xh = AND [Eb], %s", modrm, x86Reg8[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD R0, R0, R2\n" +
                            "    LDRB R3, [R0]\n" +
                            (srcHigh ?
                                    "    LSRS R2, " + srcReg + ", #8\n" :
                                    "    UXTB R2, " + srcReg + "\n") +
                            "    MOV  R1, R2\n" +         // R1 = src
                            "    MOV  R2, R3\n" +         // R2 = dst
                            "    ANDS R3, R3, R1\n" +     // R3 = dst & src
                            "    STRB R3, [R0]\n" +
                            "    MOV  R0, R2\n" +         // R0 = old dst
                            "    MOV  R2, R3\n";          // R2 = result
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + String.format("\n%s\n    ANDS R0, R0, #%d\n", rmExprs[rm], disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD R0, R0, R2\n" +
                            "    LDRB R3, [R0]\n" +
                            (srcHigh ?
                                    "    LSRS R2, " + srcReg + ", #8\n" :
                                    "    UXTB R2, " + srcReg + "\n") +
                            "    MOV  R1, R2\n" +
                            "    MOV  R2, R3\n" +
                            "    ANDS R3, R3, R1\n" +
                            "    STRB R3, [R0]\n" +
                            "    MOV  R0, R2\n" +
                            "    MOV  R2, R3\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + String.format("\n%s\n    AND  R0, R0, #%d\n", rmExprs[rm], disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD R0, R0, R2\n" +
                            "    LDRB R3, [R0]\n" +
                            (srcHigh ?
                                    "    LSRS R2, " + srcReg + ", #8\n" :
                                    "    UXTB R2, " + srcReg + "\n") +
                            "    MOV  R1, R2\n" +
                            "    MOV  R2, R3\n" +
                            "    ANDS R3, R3, R1\n" +
                            "    STRB R3, [R0]\n" +
                            "    MOV  R0, R2\n" +
                            "    MOV  R2, R3\n";
                    bytes = 4;
                } else { /// if (mod == 0b11)
                    final String[] arm8regs = {"R4", "R6", "R7", "R5", "R4", "R6", "R7", "R5"};
                    final boolean dstHigh = isHigh[rm];
                    final String dstReg = arm8regs[rm];

                    m33candidate = comment + "\n" +
                            (dstHigh ?
                                    "    LSRS R0, " + dstReg + ", #8\n" :
                                    "    UXTB R0, " + dstReg + "\n") +
                            (srcHigh ?
                                    "    LSRS R1, " + srcReg + ", #8\n" :
                                    "    UXTB R1, " + srcReg + "\n") +
                            "    ANDS R2, R0, R1\n" +
                            (dstHigh ?
                                    "    LSL  R2, R2, #8\n" +
                                            "    BIC  " + dstReg + ", " + dstReg + ", #0xFF00\n" +
                                            "    AND  " + dstReg + ", " + dstReg + ", R2\n" :
                                    "    BIC  " + dstReg + ", " + dstReg + ", #0xFF\n" +
                                            "    AND  " + dstReg + ", " + dstReg + ", R2\n");
                    bytes = 2;
                }

                // Обработка флагов
                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R2");
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0x21: { // AND Ew, Gw
                changesPF = true;

                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg16 = {"R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11"};
                final String srcReg = x86Reg16[reg];

                final String[] rmExprs = {
                        "AND  R0, R5, R8           // [BX+SI]",
                        "AND  R0, R5, R9           // [BX+DI]",
                        "AND  R0, R10, R8          // [BP+SI]",
                        "AND  R0, R10, R9          // [BP+DI]",
                        "MOV  R0, R8               // [SI]",
                        "MOV  R0, R9               // [DI]",
                        "MOV  R0, R10              // [BP]",
                        "MOV  R0, R5               // [BX]"
                };

                final String comment = String.format("    // 01h %02Xh = AND [Ew], %s", modrm, x86Reg16[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R3           // old value\n" +
                            "    ANDS R3, R3, R1       // result in R3, flags auto-updated\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ANDS R0, R0, #%d\n", disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ANDS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    AND  R0, R0, #%d\n", disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ANDS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 4;
                } else { // mod == 0b11, регистровый режим
                    final String dstReg = x86Reg16[rm];
                    m33candidate = comment + "\n" +
                            "    MOV  R0, " + dstReg + "\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R0\n" +
                            "    ANDS R3, R0, R1\n" +
                            "    MOV  " + dstReg + ", R3\n";
                    bytes = 2;
                }

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R3");
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x22: { // AND Gw, Ew
                changesPF = true;

                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg16 = {"R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11"};
                final String dstReg = x86Reg16[reg];

                final String[] rmExprs = {
                        "AND  R0, R5, R8           // [BX+SI]",
                        "AND  R0, R5, R9           // [BX+DI]",
                        "AND  R0, R10, R8          // [BP+SI]",
                        "AND  R0, R10, R9          // [BP+DI]",
                        "MOV  R0, R8               // [SI]",
                        "MOV  R0, R9               // [DI]",
                        "MOV  R0, R10              // [BP]",
                        "MOV  R0, R5               // [BX]"
                };

                final String comment = String.format("    // 02h %02Xh = AND %s, [Ew]", modrm, x86Reg16[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3           // old value\n" +
                            "    ANDS R3, R3, R1       // result in R3, flags auto-updated\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ANDS R0, R0, #%d\n", disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ANDS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    AND  R0, R0, #%d\n", disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ANDS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 4;
                } else { // mod == 0b11, регистровый режим
                    final String srcReg = x86Reg16[rm];
                    m33candidate = comment + "\n" +
                            "    MOV  R0, " + srcReg + "\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R0\n" +
                            "    ANDS R3, R0, R1\n" +
                            "    MOV  " + dstReg + ", R3\n";
                    bytes = 2;
                }

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R3");
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x23: { // AND Gw, Ew (с адресованием через память)
                changesPF = true;

                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg16 = {"R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11"};
                final String dstReg = x86Reg16[reg];

                final String[] rmExprs = {
                        "AND  R0, R5, R8           // [BX+SI]",
                        "AND  R0, R5, R9           // [BX+DI]",
                        "AND  R0, R10, R8          // [BP+SI]",
                        "AND  R0, R10, R9          // [BP+DI]",
                        "MOV  R0, R8               // [SI]",
                        "MOV  R0, R9               // [DI]",
                        "MOV  R0, R10              // [BP]",
                        "MOV  R0, R5               // [BX]"
                };

                final String comment = String.format("    // 03h %02Xh = AND %s, [Ew]", modrm, x86Reg16[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3           // old value\n" +
                            "    ANDS R3, R3, R1       // result in R3, flags auto-updated\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    ANDS R0, R0, #%d\n", disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ANDS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    AND  R0, R0, #%d\n", disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    ANDS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 4;
                } else { // mod == 0b11, регистровый режим
                    final String srcReg = x86Reg16[rm];
                    m33candidate = comment + "\n" +
                            "    MOV  R0, " + srcReg + "\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R0\n" +
                            "    ANDS R3, R0, R1\n" +
                            "    MOV  " + dstReg + ", R3\n";
                    bytes = 2;
                }

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R3");
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x24: { // AND AL, Ib
                changesPF = true;

                final int imm8 = bin.get(offset + 1); // 8-битное значение, которое прибавляем к AL (R4)

                m33candidate = String.format("" +
                                "    MOV  R0, R4              // 04h %02Xh = AND AL, (AL = R4)\n" +
                                "    UXTB R0, R0              // оставляем только младший байт в R0, дальше используется как первый аргумент\n" +
                                "    LDR  R1, =%d            // загружаем константу в R1, дальше используется, как второй аргумент\n" +
                                "    ANDS R2, R0, R1          // выполняем AND, в R2 9 бит результата\n" +
                                "    UXTB R4, R2              // результат - только младший байт сохраняем в R4 (AL)",
                        imm8 & 0xFF, imm8
                );

                bytes = 2;
                nextOffset = offset + bytes;

                // Вычисление флага чётности (PF)
                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R2");
                }
                break;
            }
            case 0x25: { // AND AX, Imm16 — в 16-битном режиме
                changesCF = true;
                changesPF = true;
                changesAF = true;

                final int imm16 = (bin.get(offset + 1) & 0xFF)
                        | ((bin.get(offset + 2) & 0xFF) << 8);

                m33candidate = String.format(
                        "    MOV   R0, R4              // 05h %04Xh = AND AX, 0x%04X (AX = R4 & 0xFFFF)\n" +
                                "    UXTH  R0, R0              // оставить только 16 бит (AX)\n" +
                                "    LDR   R1, =%d             // immediate 16-бит\n" +
                                "    ANDS  R2, R0, R1          // R2 = AX + imm16\n" +
                                "    UXTH  R4, R2              // сохранить только младшие 16 бит (AX) обратно в R4\n",
                        imm16 & 0xFFFF, imm16 & 0xFFFF, imm16
                );

                bytes = 3;
                nextOffset = offset + bytes;

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R2"); // we should use 8-bit parity for 16-bit op (historically)
                }

                break;
            }
            case 0x30: { // XOR Eb, Gb
                changesPF = true;
                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg8 = {"AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"};
                final String[] armReg8 = {"R4", "R6", "R7", "R5", "R4", "R6", "R7", "R5"};
                final boolean[] isHigh = {false, false, false, false, true, true, true, true};

                final String srcReg = armReg8[reg];
                final boolean srcHigh = isHigh[reg];

                final String[] rmExprs = {
                        "EOR  R0, R5, R8           // [BX+SI]<-",
                        "EOR  R0, R5, R9           // [BX+DI]<-",
                        "EOR  R0, R10, R8          // [BP+SI]<-",
                        "EOR  R0, R10, R9          // [BP+DI]<-",
                        "MOV  R0, R8               // [SI]<-",
                        "MOV  R0, R9               // [DI]<-",
                        "MOV  R0, R10              // [BP]<-",
                        "MOV  R0, R5               // [BX]<-"
                };

                final String comment = String.format("    // 00h %02Xh = XOR [Eb], %s", modrm, x86Reg8[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRB R3, [R0]\n" +
                            (srcHigh ?
                                    "    LSRS R2, " + srcReg + ", #8\n" :
                                    "    UXTB R2, " + srcReg + "\n") +
                            "    MOV  R1, R2\n" +         // R1 = src
                            "    MOV  R2, R3\n" +         // R2 = dst
                            "    EORS R3, R3, R1\n" +     // R3 = dst ^ src
                            "    STRB R3, [R0]\n" +
                            "    MOV  R0, R2\n" +         // R0 = old dst
                            "    MOV  R2, R3\n";          // R2 = result
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + String.format("\n%s\n    EORS R0, R0, #%d\n", rmExprs[rm], disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRB R3, [R0]\n" +
                            (srcHigh ?
                                    "    LSRS R2, " + srcReg + ", #8\n" :
                                    "    UXTB R2, " + srcReg + "\n") +
                            "    MOV  R1, R2\n" +
                            "    MOV  R2, R3\n" +
                            "    EORS R3, R3, R1\n" +
                            "    STRB R3, [R0]\n" +
                            "    MOV  R0, R2\n" +
                            "    MOV  R2, R3\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + String.format("\n%s\n    EOR  R0, R0, #%d\n", rmExprs[rm], disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRB R3, [R0]\n" +
                            (srcHigh ?
                                    "    LSRS R2, " + srcReg + ", #8\n" :
                                    "    UXTB R2, " + srcReg + "\n") +
                            "    MOV  R1, R2\n" +
                            "    MOV  R2, R3\n" +
                            "    EORS R3, R3, R1\n" +
                            "    STRB R3, [R0]\n" +
                            "    MOV  R0, R2\n" +
                            "    MOV  R2, R3\n";
                    bytes = 4;
                } else { /// if (mod == 0b11)
                    final String[] arm8regs = {"R4", "R6", "R7", "R5", "R4", "R6", "R7", "R5"};
                    final boolean dstHigh = isHigh[rm];
                    final String dstReg = arm8regs[rm];

                    m33candidate = comment + "\n" +
                            (dstHigh ?
                                    "    LSRS R0, " + dstReg + ", #8\n" :
                                    "    UXTB R0, " + dstReg + "\n") +
                            (srcHigh ?
                                    "    LSRS R1, " + srcReg + ", #8\n" :
                                    "    UXTB R1, " + srcReg + "\n") +
                            "    EORS R2, R0, R1\n" +
                            (dstHigh ?
                                    "    LSL  R2, R2, #8\n" +
                                            "    BIC  " + dstReg + ", " + dstReg + ", #0xFF00\n" +
                                            "    EOR  " + dstReg + ", " + dstReg + ", R2\n" :
                                    "    BIC  " + dstReg + ", " + dstReg + ", #0xFF\n" +
                                            "    EOR  " + dstReg + ", " + dstReg + ", R2\n");
                    bytes = 2;
                }

                // Обработка флагов
                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R2");
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0x31: { // XOR Ew, Gw
                changesPF = true;

                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg16 = {"R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11"};
                final String srcReg = x86Reg16[reg];

                final String[] rmExprs = {
                        "EOR  R0, R5, R8           // [BX+SI]",
                        "EOR  R0, R5, R9           // [BX+DI]",
                        "EOR  R0, R10, R8          // [BP+SI]",
                        "EOR  R0, R10, R9          // [BP+DI]",
                        "MOV  R0, R8               // [SI]",
                        "MOV  R0, R9               // [DI]",
                        "MOV  R0, R10              // [BP]",
                        "MOV  R0, R5               // [BX]"
                };

                final String comment = String.format("    // 01h %02Xh = XOR [Ew], %s", modrm, x86Reg16[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R3           // old value\n" +
                            "    EORS R3, R3, R1       // result in R3, flags auto-updated\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    EORS R0, R0, #%d\n", disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    EORS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    EOR  R0, R0, #%d\n", disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    EORS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 4;
                } else { // mod == 0b11, регистровый режим
                    final String dstReg = x86Reg16[rm];
                    m33candidate = comment + "\n" +
                            "    MOV  R0, " + dstReg + "\n" +
                            "    MOV  R1, " + srcReg + "\n" +
                            "    MOV  R2, R0\n" +
                            "    EORS R3, R0, R1\n" +
                            "    MOV  " + dstReg + ", R3\n";
                    bytes = 2;
                }

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R3");
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x32: { // XOR Gw, Ew
                changesPF = true;

                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg16 = {"R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11"};
                final String dstReg = x86Reg16[reg];

                final String[] rmExprs = {
                        "EOR  R0, R5, R8           // [BX+SI]",
                        "EOR  R0, R5, R9           // [BX+DI]",
                        "EOR  R0, R10, R8          // [BP+SI]",
                        "EOR  R0, R10, R9          // [BP+DI]",
                        "MOV  R0, R8               // [SI]",
                        "MOV  R0, R9               // [DI]",
                        "MOV  R0, R10              // [BP]",
                        "MOV  R0, R5               // [BX]"
                };

                final String comment = String.format("    // 02h %02Xh = XOR %s, [Ew]", modrm, x86Reg16[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3           // old value\n" +
                            "    EORS R3, R3, R1       // result in R3, flags auto-updated\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    EORS R0, R0, #%d\n", disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    EORS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    EOR  R0, R0, #%d\n", disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    EORS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 4;
                } else { // mod == 0b11, регистровый режим
                    final String srcReg = x86Reg16[rm];
                    m33candidate = comment + "\n" +
                            "    MOV  R0, " + srcReg + "\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R0\n" +
                            "    EORS R3, R0, R1\n" +
                            "    MOV  " + dstReg + ", R3\n";
                    bytes = 2;
                }

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R3");
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x33: { // XOR Gw, Ew (с адресованием через память)
                changesPF = true;

                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;

                final String[] x86Reg16 = {"R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11"};
                final String dstReg = x86Reg16[reg];

                final String[] rmExprs = {
                        "EOR  R0, R5, R8           // [BX+SI]",
                        "EOR  R0, R5, R9           // [BX+DI]",
                        "EOR  R0, R10, R8          // [BP+SI]",
                        "EOR  R0, R10, R9          // [BP+DI]",
                        "MOV  R0, R8               // [SI]",
                        "MOV  R0, R9               // [DI]",
                        "MOV  R0, R10              // [BP]",
                        "MOV  R0, R5               // [BX]"
                };

                final String comment = String.format("    // 03h %02Xh = XOR %s, [Ew]", modrm, x86Reg16[reg]);

                if (mod == 0b00) {
                    m33candidate = comment + "\n    " + rmExprs[rm] + "\n" +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3           // old value\n" +
                            "    EORS R3, R3, R1       // result in R3, flags auto-updated\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 2;
                } else if (mod == 0b01) {
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    EORS R0, R0, #%d\n", disp8s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    EORS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 3;
                } else if (mod == 0b10) {
                    final int lo = bin.get(offset + 2) & 0xFF;
                    final int hi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (hi << 8) | lo;
                    final int disp16s = (short) disp16;
                    m33candidate = comment + "\n    " + rmExprs[rm] +
                            String.format("\n    EOR  R0, R0, #%d\n", disp16s) +
                            "    LDR  R1, =X86_DS\n" +
                            "    LDRH R2, [R1]\n" +
                            "    LSLS R2, R2, #4\n" +
                            "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                            "    ADD  R2, R2, R3\n" +
                            "    ADD  R0, R0, R2\n" +
                            "    LDRH R3, [R0]\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R3\n" +
                            "    EORS R3, R3, R1\n" +
                            "    STRH R3, [R0]\n";
                    bytes = 4;
                } else { // mod == 0b11, регистровый режим
                    final String srcReg = x86Reg16[rm];
                    m33candidate = comment + "\n" +
                            "    MOV  R0, " + srcReg + "\n" +
                            "    MOV  R1, " + dstReg + "\n" +
                            "    MOV  R2, R0\n" +
                            "    EORS R3, R0, R1\n" +
                            "    MOV  " + dstReg + ", R3\n";
                    bytes = 2;
                }

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R3");
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x34: { // XOR AL, Ib
                changesPF = true;

                final int imm8 = bin.get(offset + 1); // 8-битное значение, которое прибавляем к AL (R4)

                m33candidate = String.format("" +
                                "    MOV  R0, R4              // 04h %02Xh = XOR AL, (AL = R4)\n" +
                                "    UXTB R0, R0              // оставляем только младший байт в R0, дальше используется как первый аргумент\n" +
                                "    LDR  R1, =%d            // загружаем константу в R1, дальше используется, как второй аргумент\n" +
                                "    EORS R2, R0, R1          // выполняем XOR, в R2 9 бит результата\n" +
                                "    UXTB R4, R2              // результат - только младший байт сохраняем в R4 (AL)",
                        imm8 & 0xFF, imm8
                );

                bytes = 2;
                nextOffset = offset + bytes;

                // Вычисление флага чётности (PF)
                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R2");
                }
                break;
            }
            case 0x35: { // XOR AX, Imm16 — в 16-битном режиме
                changesCF = true;
                changesPF = true;
                changesAF = true;

                final int imm16 = (bin.get(offset + 1) & 0xFF)
                        | ((bin.get(offset + 2) & 0xFF) << 8);

                m33candidate = String.format(
                        "    MOV   R0, R4              // 05h %04Xh = XOR AX, 0x%04X (AX = R4 & 0xFFFF)\n" +
                                "    UXTH  R0, R0              // оставить только 16 бит (AX)\n" +
                                "    LDR   R1, =%d             // immediate 16-бит\n" +
                                "    EORS  R2, R0, R1          // R2 = AX ^ imm16\n" +
                                "    UXTH  R4, R2              // сохранить только младшие 16 бит (AX) обратно в R4\n",
                        imm16 & 0xFFFF, imm16 & 0xFFFF, imm16
                );

                bytes = 3;
                nextOffset = offset + bytes;

                if (this.PFrequired) {
                    m33candidate += "\n" + emitPF8("R2"); // we should use 8-bit parity for 16-bit op (historically)
                }

                break;
            }
            case 0x70: { // JO
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // relative jump to (signed byte)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format("    BVS m%08X            // 70h %02Xh = JO %+d (m%08X)", pointsTo, signedByte & 0xFF, signedByte, pointsTo);
                break;
            }
            case 0x71: { // JNO
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // relative jump to (signed byte)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format("    BVC m%08X            // 71h %02Xh = JNO %+d (m%08X)", pointsTo, signedByte & 0xFF, signedByte, pointsTo);
                break;
            }
            case 0x72: { // JB / JC
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // relative jump to (signed byte)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format("    BCS m%08X            // 72h %02Xh = JC %+d (m%08X)", pointsTo, signedByte & 0xFF, signedByte, pointsTo);
                CFback = true;
                break;
            }
            case 0x73: { // JAE / JNB / JNC
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // relative jump to (signed byte)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format("    BCC m%08X            // 73h %02Xh = JNC %+d (m%08X)", pointsTo, signedByte & 0xFF, signedByte, pointsTo);
                CFback = true;
                break;
            }
            case 0x74: { // JE / JZ
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // relative jump to (signed byte)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format("    BEQ m%08X            // 74h %02Xh = JZ %+d (m%08X)", pointsTo, signedByte & 0xFF, signedByte, pointsTo);
                break;
            }
            case 0x75: { // JNE / JNZ
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // relative jump to (signed byte)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format("    BNE m%08X            // 75h %02Xh = JNZ %+d (m%08X)", pointsTo, signedByte & 0xFF, signedByte, pointsTo);
                break;
            }
            case 0x76: { // JBE / JNA
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // relative jump to (signed byte)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format("    BLS m%08X            // 76h %02Xh = JBE %+d (m%08X)", pointsTo, signedByte & 0xFF, signedByte, pointsTo);
                break;
            }
            case 0x77: { // JA / JNBE
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // relative jump to (signed byte)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format("    BHI m%08X            // 77h %02Xh = JA %+d (m%08X)", pointsTo, signedByte & 0xFF, signedByte, pointsTo);
                break;
            }
            case 0x78: { // JS
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // относительное смещение (signed)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format("    BMI m%08X            // 78h %02Xh = JS %+d (m%08X)", pointsTo, signedByte & 0xFF, signedByte, pointsTo);
                break;
            }
            case 0x79: { // JNS
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // относительное смещение (signed)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format("    BPL m%08X            // 79h %02Xh = JNS %+d (m%08X)", pointsTo, signedByte & 0xFF, signedByte, pointsTo);
                break;
            }
            case 0x7A: { // JP / JPE
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // относительное смещение (signed)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format(
                        "    MRS  R1, APSR               // JP parity W/A\n" +
                        "    LDRB R0, =X86_PF            // загрузить адрес переменной X86_PF\n" +
                        "    LDRB R0, [R0]               // загрузить значение флага чётности\n" +
                        "    CMP  R0, #1                 // JP, если PF == 1\n" +
                        "    BEQ  m%08X_P                // 7Ah %02Xh = JP %+d (m%08X_P)\n" +
                        "    MSR  APSR_nzcvq, R1         // восстановить только N,Z,C,V",
                        pointsTo, signedByte & 0xFF, signedByte, pointsTo);
                PFback = true;
                break;
            }
            case 0x7B: { // JNP / JPO
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // относительное смещение (signed)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format(
                        "    MRS  R1, APSR               // JNP parity W/A\n" +
                        "    LDRB R0, =X86_PF            // загрузить адрес переменной X86_PF\n" +
                        "    LDRB R0, [R0]               // загрузить значение флага чётности\n" +
                        "    CMP  R0, #1                 // JNP, если PF == 0\n" +
                        "    BNE  m%08X_P                // 7Bh %02Xh = JNP %+d (m%08X_P)\n" +
                        "    MSR  APSR_nzcvq, R1         // восстановить только N,Z,C,V",
                        pointsTo, signedByte & 0xFF, signedByte, pointsTo);
                PFback = true;
                break;
            }
            case 0x8C: { // MOV r/m16, Sreg
                // Копирует сегментный регистр (например, CS, DS, ES, FS, GS, SS) в 16-битный регистр или ячейку памяти.
                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;
                final String[] segmentRegs = {"ES", "CS", "SS", "DS", "FS", "GS"};
                final String sregName = (reg < segmentRegs.length) ? segmentRegs[reg] : "??"; // error case
                final String globalVar = "X86_" + sregName;

                if (mod == 0b00) { // rm указывает на адрес в памяти без смещения
                    bytes = 2;
                    final String[] rmExprs = {
                            "ADD  R0, R5, R8      // [BX+SI]", // 0
                            "ADD  R0, R5, R9      // [BX+DI]", // 1
                            "ADD  R0, R10, R8     // [BP+SI]", // 2
                            "ADD  R0, R10, R9     // [BP+DI]", // 3
                            "MOV  R0, R8          // [SI]",    // 4
                            "MOV  R0, R9          // [DI]",    // 5
                            "MOV  R0, R10         // [BP]",    // 6
                            "MOV  R0, R5          // [BX]"     // 7
                    };
                    m33candidate = String.format("// 8Ch %02Xh = MOV [r/m16], %s\n" +
                                    rmExprs[rm] +
                                    "\n    LDR  R1, =%s      // %s\n" +
                                    "    LDRH R2, [R1]\n" +
                                    "    LSLS R2, R2, #4\n" +
                                    "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                                    "    ADD  R2, R2, R3\n" +
                                    "    ADD  R0, R0, R2\n" +
                                    "    LDRH R4, [R1]\n" +
                                    "    STRH R4, [R0]",
                            modrm, sregName, globalVar, globalVar
                    );

                } else if (mod == 0b11) { // регистр-регистр
                    bytes = 2;
                    final String[] x86Regs = {"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"};
                    final String[] armRegs = {"R4", "R6", "R7", "R5", "R11", "R10", "R8", "R9"}; // ARM-регистры, соответствующие x86 (SP -> R11 временный, требуется отдельная обработка)

                    final String x86DestReg = x86Regs[rm];
                    final String armDestReg = armRegs[rm];

                    m33candidate = String.format("" +
                                    "    LDR  R0, =%s // 8Ch %02Xh = MOV %s, %s\n" +
                                    "    LDRH %s, [R0]    // x86 reg %s = %s m33 (16 бит)",
                            globalVar, modrm, x86DestReg, sregName, armDestReg, x86DestReg, armDestReg
                    );
                    if ("SP".equals(x86DestReg)) { // workaround, требуется установить правильный m33 SP
                        m33candidate +=
                                "    LDR  R1, =X86_SS              // загрузка адреса X86_SS\n" +
                                "    LDRH R2, [R1]                 // загрузка значения сегмента SS\n" +
                                "    LSLS R2, R2, #4               // R2 <<= 4 (сдвиг сегмента на 4 бита)\n" +
                                "    LDR  R3, =X86_RAM_BASE_ADDR   // база RAM\n" +
                                "    ADD  R2, R2, R3               // R2 = SS + RAM_BASE\n" +
                                "    ADD  SP, R11, R2              // SP = R11 + (SS + RAM_BASE)";
                    }
                } else if (mod == 0b01) { // память с 8-битным смещением
                    bytes = 3;
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8; // sign-extend
                    final String[] rmExprs = {
                            "ADD  R0, R5, R8      // [BX+SI]",
                            "ADD  R0, R5, R9      // [BX+DI]",
                            "ADD  R0, R10, R8     // [BP+SI]",
                            "ADD  R0, R10, R9     // [BP+DI]",
                            "MOV  R0, R8          // [SI]",
                            "MOV  R0, R9          // [DI]",
                            "MOV  R0, R10         // [BP]",
                            "MOV  R0, R5          // [BX]"
                    };
                    m33candidate = String.format("// 8Ch %02Xh %02Xh = MOV [rm16+disp8], %s\n" +
                                    rmExprs[rm] + "\n" +
                                    "    ADDS R0, R0, #%d      // смещение %+d\n" +
                                    "    LDR  R1, =%s          // %s\n" +
                                    "    LDRH R2, [R1]\n" +
                                    "    LSLS R2, R2, #4\n" +
                                    "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                                    "    ADD  R2, R2, R3\n" +
                                    "    ADD  R0, R0, R2\n" +
                                    "    LDRH R4, [R1]\n" +
                                    "    STRH R4, [R0]",
                            modrm, disp8, sregName, disp8s, disp8s,
                            globalVar, globalVar
                    );
                } else if (mod == 0b10) { // память с 16-битным смещением
                    bytes = 4;
                    final int dispLo = bin.get(offset + 2) & 0xFF;
                    final int dispHi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (dispHi << 8) | dispLo;
                    final int disp16s = (short) disp16; // sign-extend
                    final String[] rmExprs = {
                            "ADD  R0, R5, R8      // [BX+SI]",
                            "ADD  R0, R5, R9      // [BX+DI]",
                            "ADD  R0, R10, R8     // [BP+SI]",
                            "ADD  R0, R10, R9     // [BP+DI]",
                            "MOV  R0, R8          // [SI]",
                            "MOV  R0, R9          // [DI]",
                            "MOV  R0, R10         // [BP]",
                            "MOV  R0, R5          // [BX]"
                    };
                    m33candidate = String.format("// 8Ch %02Xh %02Xh %02Xh = MOV [rm16+disp16], %s\n" +
                                    rmExprs[rm] + "\n" +
                                    "    ADD  R0, R0, #%d     // смещение %+d\n" +
                                    "    LDR  R1, =%s         // %s\n" +
                                    "    LDRH R2, [R1]\n" +
                                    "    LSLS R2, R2, #4\n" +
                                    "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                                    "    ADD  R2, R2, R3\n" +
                                    "    ADD  R0, R0, R2\n" +
                                    "    LDRH R4, [R1]\n" +
                                    "    STRH R4, [R0]",
                            modrm, dispLo, dispHi, sregName, disp16s, disp16s,
                            globalVar, globalVar
                    );
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0x8A: { // MOV r8, r/m8
                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm  = modrm & 0b111;

                final String[] x86Regs = {"AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"};
                final String[] armRegs = {"R4", "R6", "R7", "R5", "R4", "R6", "R7", "R5"}; // условно для младших/старших байтов, нужен кастом
                final String dstReg = armRegs[reg];
                final String dstName = x86Regs[reg];

                if (mod == 0b11) { // регистр-регистр
                    final String[] srcArmRegs = {"R4", "R6", "R7", "R5", "R4", "R6", "R7", "R5"};
                    final String srcReg = srcArmRegs[rm];
                    m33candidate = String.format(
                            "    // 8Ah %02Xh = MOV %s, %s\n" +
                            "    UXTB %s, %s", // обрезаем до 8 бит
                            modrm, dstName, x86Regs[rm], dstReg, srcReg
                    );
                    bytes = 2;
                } else {
                    // обработка доступа к памяти — только mod==00 реализуем как пример
                    if (mod == 0b00) {
                        final String[] rmExprs = {
                                "ADD R0, R5, R8      // [BX+SI]",
                                "ADD R0, R5, R9      // [BX+DI]",
                                "ADD R0, R10, R8     // [BP+SI]",
                                "ADD R0, R10, R9     // [BP+DI]",
                                "MOV R0, R8          // [SI]",
                                "MOV R0, R9          // [DI]",
                                "MOV R0, R10         // [BP]",
                                "MOV R0, R5          // [BX]"
                        };

                        m33candidate = String.format(
                                "    // 8Ah %02Xh = MOV %s, [mem8]\n" +
                                "%s\n" +
                                "    LDR  R1, =X86_DS\n" +
                                "    LDRH R2, [R1]\n" +
                                "    LSLS R2, R2, #4\n" +
                                "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                                "    ADD  R2, R2, R3\n" +
                                "    ADD  R0, R0, R2\n" +
                                "    LDRB %s, [R0]",
                                modrm, dstName,
                                rmExprs[rm],
                                dstReg
                        );
                        bytes = 2;
                    } else {
                        m33candidate = String.format("// [WARN] 8Ah с mod=%d пока не реализован (x86 %08X: 8Ah %02Xh)", mod, offset, modrm);
                        bytes = 2;
                    }
                }

                nextOffset = offset + bytes;
                break;
            }
            case 0x8E: { // MOV Sreg, r/m16
                final int modrm = bin.get(offset + 1) & 0xFF;
                final int mod = (modrm >> 6) & 0b11;
                final int reg = (modrm >> 3) & 0b111;
                final int rm = modrm & 0b111;
                final String[] segmentRegs = {"ES", "CS", "SS", "DS", "FS", "GS"};
                final String sregName = (reg < segmentRegs.length) ? segmentRegs[reg] : "??";
                final String globalVar = "X86_" + sregName;

                if ("CS".equals(sregName)) {
                    m33candidate = String.format("    // [ERROR] MOV to CS is invalid (x86 %08X: 8Eh %02Xh)", offset, modrm);
                    bytes = 2;
                } else if (mod == 0b00) {
                    bytes = 2;
                    final String[] rmExprs = {
                            "ADD  R0, R5, R8      // [BX+SI]",
                            "ADD  R0, R5, R9      // [BX+DI]",
                            "ADD  R0, R10, R8     // [BP+SI]",
                            "ADD  R0, R10, R9     // [BP+DI]",
                            "MOV  R0, R8          // [SI]",
                            "MOV  R0, R9          // [DI]",
                            "MOV  R0, R10         // [BP]",
                            "MOV  R0, R5          // [BX]"
                    };
                    m33candidate = String.format("   // 8Eh %02Xh = MOV %s, [r/m16]\n" +
                                    rmExprs[rm] + "\n" +
                                    "    LDR  R1, =X86_DS\n" +
                                    "    LDRH R2, [R1]\n" +
                                    "    LSLS R2, R2, #4\n" +
                                    "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                                    "    ADD  R2, R2, R3\n" +
                                    "    ADD  R0, R0, R2\n" +
                                    "    LDRH R4, [R0]\n" +
                                    "    LDR  R5, =%s\n" +
                                    "    STRH R4, [R5]",
                            modrm, sregName, globalVar
                    );
                } else if (mod == 0b01) {
                    bytes = 3;
                    final int disp8 = bin.get(offset + 2);
                    final int disp8s = (byte) disp8;
                    final String[] rmExprs = {
                            "ADD  R0, R5, R8      // [BX+SI]",
                            "ADD  R0, R5, R9      // [BX+DI]",
                            "ADD  R0, R10, R8     // [BP+SI]",
                            "ADD  R0, R10, R9     // [BP+DI]",
                            "MOV  R0, R8          // [SI]",
                            "MOV  R0, R9          // [DI]",
                            "MOV  R0, R10         // [BP]",
                            "MOV  R0, R5          // [BX]"
                    };
                    m33candidate = String.format("    // 8Eh %02Xh %02Xh = MOV %s, [r/m16 + %d]\n" +
                                    rmExprs[rm] + "\n" +
                                    "    ADDS R0, R0, #%d\n" +
                                    "    LDR  R1, =X86_DS\n" +
                                    "    LDRH R2, [R1]\n" +
                                    "    LSLS R2, R2, #4\n" +
                                    "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                                    "    ADD  R2, R2, R3\n" +
                                    "    ADD  R0, R0, R2\n" +
                                    "    LDRH R4, [R0]\n" +
                                    "    LDR  R5, =%s\n" +
                                    "    STRH R4, [R5]",
                            modrm, disp8, sregName, disp8s, disp8s, globalVar
                    );
                } else if (mod == 0b10) {
                    bytes = 4;
                    final int dispLo = bin.get(offset + 2) & 0xFF;
                    final int dispHi = bin.get(offset + 3) & 0xFF;
                    final int disp16 = (dispHi << 8) | dispLo;
                    final int disp16s = (short) disp16;
                    final String[] rmExprs = {
                            "ADD  R0, R5, R8      // [BX+SI]",
                            "ADD  R0, R5, R9      // [BX+DI]",
                            "ADD  R0, R10, R8     // [BP+SI]",
                            "ADD  R0, R10, R9     // [BP+DI]",
                            "MOV  R0, R8          // [SI]",
                            "MOV  R0, R9          // [DI]",
                            "MOV  R0, R10         // [BP]",
                            "MOV  R0, R5          // [BX]"
                    };
                    m33candidate = String.format("    // 8Eh %02Xh %02Xh %02Xh = MOV %s, [r/m16 + %d]\n" +
                                    rmExprs[rm] + "\n" +
                                    "    ADD  R0, R0, #%d\n" +
                                    "    LDR  R1, =X86_DS\n" +
                                    "    LDRH R2, [R1]\n" +
                                    "    LSLS R2, R2, #4\n" +
                                    "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                                    "    ADD  R2, R2, R3\n" +
                                    "    ADD  R0, R0, R2\n" +
                                    "    LDRH R4, [R0]\n" +
                                    "    LDR  R5, =%s\n" +
                                    "    STRH R4, [R5]",
                            modrm, dispLo, dispHi, sregName, disp16s, disp16s, globalVar
                    );
                } else {
                    bytes = 2;
                    final String[] x86Regs = {"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"};
                    final String[] armRegs = {"R4", "R6", "R7", "R5", "R11", "R10", "R8", "R9"};

                    final String x86SrcReg = x86Regs[rm];
                    final String armSrcReg = armRegs[rm];

                    m33candidate = String.format("    // 8Eh %02Xh = MOV %s, %s\n" +
                                    "    STRH %s, [%s]",
                            modrm, sregName, x86SrcReg, armSrcReg, globalVar
                    );
                    if ("SP".equals(x86SrcReg)) {
                        m33candidate += "\n    LDR  R1, =X86_SS\n" +
                                "    LDRH R2, [R1]\n" +
                                "    LSLS R2, R2, #4\n" +
                                "    LDR  R3, =X86_RAM_BASE_ADDR\n" +
                                "    ADD  R2, R2, R3\n" +
                                "    ADD  SP, R11, R2";
                    }
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0x90: // NOP
                m33candidate ="    NOP                  // 90h = NOP";
                nextOffset = offset + bytes;
                break;
            case 0xB0: { // MOV AL, imm8
                bytes = 2;
                final int b1 = bin.get(offset + 1) & 0xFF;
                if (b1 == 0) {
                    m33candidate = "    AND R4, R4, #0xFFFFFF00   // B0h 00h = MOV AL, 0";
                } else {
                    m33candidate = String.format(
                            "    LDR  R0, =0x%02X            // B0h %02Xh = MOV AL, 0x%02X\n" +
                                    "    AND  R4, R4, #0xFFFFFF00  // \n" +
                                    "    ORR  R4, R4, R0           // объединить AL и новый AH"
                            , b1, b1, b1
                    );
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0xB1: { // MOV CL, imm8
                bytes = 2;
                final int b1 = bin.get(offset + 1) & 0xFF;
                if (b1 == 0) {
                    m33candidate = "    AND R6, R6, #0xFFFFFF00   // B1h 00h = MOV CL, 0";
                } else {
                    m33candidate = String.format(
                            "    LDR  R0, =0x%02X            // B1h %02Xh = MOV CL, 0x%02X\n" +
                                    "    AND  R6, R6, #0xFFFFFF00  // \n" +
                                    "    ORR  R6, R6, R0           // объединить CL и новый CH"
                            , b1, b1, b1
                    );
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0xB2: { // MOV DL, imm8
                bytes = 2;
                final int b1 = bin.get(offset + 1) & 0xFF;
                if (b1 == 0) {
                    m33candidate = "    AND R7, R7, #0xFFFFFF00   // B2h 00h = MOV DL, 0";
                } else {
                    m33candidate = String.format(
                            "    LDR  R0, =0x%02X            // B2h %02Xh = MOV DL, 0x%02X\n" +
                                    "    AND  R7, R7, #0xFFFFFF00  // \n" +
                                    "    ORR  R7, R7, R0           // объединить DL и новый DH"
                            , b1, b1, b1
                    );
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0xB3: { // MOV BL, imm8
                bytes = 2;
                final int b1 = bin.get(offset + 1) & 0xFF;
                if (b1 == 0) {
                    m33candidate = "    AND R5, R5, #0xFFFFFF00   // B3h 00h = MOV BL, 0";
                } else {
                    m33candidate = String.format(
                            "    LDR  R0, =0x%02X            // B3h %02Xh = MOV BL, 0x%02X\n" +
                                    "    AND  R5, R5, #0xFFFFFF00  // \n" +
                                    "    ORR  R5, R5, R0           // объединить BL и новый BH"
                            , b1, b1, b1
                    );
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0xB4: { // MOV AH, imm8
                bytes = 2;
                final int b1 = bin.get(offset + 1) & 0xFF;
                if (b1 == 0) {
                    m33candidate = "    AND R4, R4, #0xFFFF00FF   // B4h 00h = MOV AH, 0 - Обнуляем старший байт";
                } else {
                    m33candidate = String.format(
                            "    LDR  R0, =0x%02X            // B4h %02Xh = MOV AH, 0x%02X\n" +
                            "    LSL  R0, R0, #8           // imm8 → в позицию AH\n" +
                            "    AND  R4, R4, #0xFFFF00FF  // Обнуляем старший байт\n" +
                            "    ORR  R4, R4, R0           // объединить AL и новый AH"
                            , b1, b1, b1
                    );
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0xB5: { // MOV CH, imm8
                bytes = 2;
                final int b1 = bin.get(offset + 1) & 0xFF;
                if (b1 == 0) {
                    m33candidate = "    AND R6, R6, #0xFFFF00FF   // B5h 00h = MOV CH, 0 - Обнуляем старший байт";
                } else {
                    m33candidate = String.format(
                            "    LDR  R0, =0x%02X            // B5h %02Xh = MOV CH, 0x%02X\n" +
                                    "    LSL  R0, R0, #8           // imm8 → в позицию CH\n" +
                                    "    AND  R6, R6, #0xFFFF00FF  // Обнуляем старший байт\n" +
                                    "    ORR  R6, R6, R0           // объединить CL и новый CH"
                            , b1, b1, b1
                    );
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0xB6: { // MOV DH, imm8
                bytes = 2;
                final int b1 = bin.get(offset + 1) & 0xFF;
                if (b1 == 0) {
                    m33candidate = "    AND R7, R7, #0xFFFF00FF   // D6h 00h = MOV DH, 0 - Обнуляем старший байт";
                } else {
                    m33candidate = String.format(
                            "    LDR  R0, =0x%02X            // B6h %02Xh = MOV DH, 0x%02X\n" +
                                    "    LSL  R0, R0, #8           // imm8 → в позицию DH\n" +
                                    "    AND  R7, R7, #0xFFFF00FF  // Обнуляем старший байт\n" +
                                    "    ORR  R7, R7, R0           // объединить DL и новый DH"
                            , b1, b1, b1
                    );
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0xB7: { // MOV BH, imm8
                bytes = 2;
                final int b1 = bin.get(offset + 1) & 0xFF;
                if (b1 == 0) {
                    m33candidate = "    AND R5, R5, #0xFFFF00FF   // D7h 00h = MOV BH, 0 - Обнуляем старший байт";
                } else {
                    m33candidate = String.format(
                            "    LDR  R0, =0x%02X            // B7h %02Xh = MOV BH, 0x%02X\n" +
                                    "    LSL  R0, R0, #8           // imm8 → в позицию BH\n" +
                                    "    AND  R5, R5, #0xFFFF00FF  // Обнуляем старший байт\n" +
                                    "    ORR  R5, R5, R0           // объединить BL и новый BH"
                            , b1, b1, b1
                    );
                }
                nextOffset = offset + bytes;
                break;
            }
            case 0xB8: { // MOV AX, imm16 (в 16-битном режиме), MOV EAX, imm32 (в 32-битном режиме)
                // Следующие 2 или 4 байта — это непосредственное значение (иммедиат)
                bytes = 3;
                final int b1 = bin.get(offset + 1) & 0xFF;
                final int b2 = bin.get(offset + 2) & 0xFF;
                m33candidate = String.format(
                        "    MOV  R4, #0x%02X%02X          // B8h %02Xh %02Xh = MOV AX, 0x%02X%02X",
                        b2, b1, b1, b2, b2, b1
                );
                nextOffset = offset + bytes;
                break;
            }
            case 0xB9: { // MOV CX, imm16 (в 16-битном режиме), MOV ECX, imm32 (в 32-битном режиме)
                // Следующие 2 или 4 байта — это непосредственное значение (иммедиат)
                bytes = 3;
                final int b1 = bin.get(offset + 1) & 0xFF;
                final int b2 = bin.get(offset + 2) & 0xFF;
                m33candidate = String.format(
                        "    MOV  R6, #0x%02X%02X          // B9h %02Xh %02Xh = MOV CX, 0x%02X%02X",
                        b2, b1, b1, b2, b2, b1
                );
                nextOffset = offset + bytes;
                break;
            }
            case 0xBA: { // MOV DX, imm16 (в 16-битном режиме), MOV EDX, imm32 (в 32-битном режиме)
                // Следующие 2 или 4 байта — это непосредственное значение (иммедиат)
                bytes = 3;
                final int b1 = bin.get(offset + 1) & 0xFF;
                final int b2 = bin.get(offset + 2) & 0xFF;
                m33candidate = String.format(
                        "    MOV  R7, #0x%02X%02X          // BAh %02Xh %02Xh = MOV DX, 0x%02X%02X",
                        b2, b1, b1, b2, b2, b1
                );
                nextOffset = offset + bytes;
                break;
            }
            case 0xBB: { // MOV BX, imm16 (в 16-битном режиме), MOV EDX, imm32 (в 32-битном режиме)
                // Следующие 2 или 4 байта — это непосредственное значение (иммедиат)
                bytes = 3;
                final int b1 = bin.get(offset + 1) & 0xFF;
                final int b2 = bin.get(offset + 2) & 0xFF;
                m33candidate = String.format(
                        "    MOV  R5, #0x%02X%02X          // BBh %02Xh %02Xh = MOV BX, 0x%02X%02X",
                        b2, b1, b1, b2, b2, b1
                );
                nextOffset = offset + bytes;
                break;
            }
            case 0xBC: { // MOV SP, imm16 (в 16-битном режиме), MOV ESP, imm32 (в 32-битном режиме)
                // Следующие 2 или 4 байта — это непосредственное значение (иммедиат)
                bytes = 3;
                final int b1 = bin.get(offset + 1) & 0xFF;
                final int b2 = bin.get(offset + 2) & 0xFF;
                m33candidate = String.format(
                        "    MOV  R0, #0x%02X%02X          // BCh %02Xh %02Xh = MOV SP, 0x%02X%02X, TODO: M33_SP = X86_SS * 16 + X86_SP\n",
                        b2, b1, b1, b2, b2, b1
                );
                nextOffset = offset + bytes;
                break;
            }
            case 0xBD: { // MOV BP, imm16 (в 16-битном режиме), MOV EDX, imm32 (в 32-битном режиме)
                // Следующие 2 или 4 байта — это непосредственное значение (иммедиат)
                bytes = 3;
                final int b1 = bin.get(offset + 1) & 0xFF;
                final int b2 = bin.get(offset + 2) & 0xFF;
                m33candidate = String.format(
                        "    MOV  R10, #0x%02X%02X          // BDh %02Xh %02Xh = MOV BP, 0x%02X%02X",
                        b2, b1, b1, b2, b2, b1
                );
                nextOffset = offset + bytes;
                break;
            }
            case 0xBE: { // MOV SI, imm16 (в 16-битном режиме), MOV ESI, imm32 (в 32-битном режиме)
                // Следующие 2 или 4 байта — это непосредственное значение (иммедиат)
                bytes = 3;
                final int b1 = bin.get(offset + 1) & 0xFF;
                final int b2 = bin.get(offset + 2) & 0xFF;
                m33candidate = String.format(
                        "    MOV  R8, #0x%02X%02X          // BEh %02Xh %02Xh = MOV SI, 0x%02X%02X",
                        b2, b1, b1, b2, b2, b1
                );
                nextOffset = offset + bytes;
                break;
            }
            case 0xBF: { // MOV DI, imm16 (в 16-битном режиме), MOV EDI, imm32 (в 32-битном режиме)
                // Следующие 2 или 4 байта — это непосредственное значение (иммедиат)
                bytes = 3;
                final int b1 = bin.get(offset + 1) & 0xFF;
                final int b2 = bin.get(offset + 2) & 0xFF;
                m33candidate = String.format(
                        "    MOV  R9, #0x%02X%02X          // BFh %02Xh %02Xh = MOV DI, 0x%02X%02X",
                        b2, b1, b1, b2, b2, b1
                );
                nextOffset = offset + bytes;
                break;
            }
            case 0xC3: // RET
                m33candidate = "    BX LR                // C3h = RET";
                nextOffset = -1; // no direct pass
                break;
            case 0xCC: // int3
                nextOffset = offset + bytes;
                pointsTo = nextOffset;
                m33candidate = String.format(
                        "    push {lr}                 // не потерять в INT/IRET\n" +
                        "    adr  r11, m%08X_IRET  // CCh = int3\n" +
                        "    mrs  r12, apsr            // Сохраняем флаги\n" +
                        "    push {r11, r12}           // Эмулируем PUSH IP, PUSH FLAGS\n" +
                        "    ldr  r11, =0x11000012     // Адрес обработчика INT3h\n" +
                        "    ldr  r11, [r11]           // Адрес обработчика (IDT)\n" +
                        "    mov  pc, r11              // Переход к обработчику\n" +
                        "m%08X_IRET:\n"+
                        "    pop  {lr}                 // восстановить после IRET"
                        , offset, offset
                );
                nextOffset = -1; // no direct pass
                break;
            case 0xCD: { // INT imm8
                bytes = 2;
                nextOffset = offset + bytes;
                pointsTo = nextOffset;
                final int b1 = bin.get(offset + 1) & 0xFF;
                m33candidate = String.format(
                        "    CPSID i                   // Запрет прерываний перед INT\n" +
                        "    push {lr}                 // не потерять в INT/IRET\n" +
                        "    adr  r11, m%08X_IRET   // CCh = int %02X\n" +
                        "    mrs  r12, apsr            // Сохраняем флаги\n" +
                        "    push {r11, r12}           // Эмулируем PUSH IP, PUSH FLAGS\n" +
                        "    ldr  r11, =0x%08X     // Адрес обработчика INT %02Xh\n" +
                        "    ldr  r11, [r11]           // Адрес обработчика (IDT)\n" +
                        "    mov  pc, r11              // Переход к обработчику\n"+
                        "m%08X_IRET:\n"+
                        "    pop  {lr}                 // восстановить после IRET"
                        , offset, b1, 0x11000000 + (b1 * 4), b1, offset
                );
                nextOffset = -1; // no direct pass
                break;
            }
            case 0xE8: { // CALL rel16/32
                bytes = 3;
                final int b1 = bin.get(offset + 1) & 0xFF;
                final int b2 = bin.get(offset + 2);
                final int signedInt = (b2 << 8) | b1;
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedInt;
                if (pointsTo < 0) {
                    pointsTo += 0x10000;
                }
                m33candidate = String.format(
                        "    push {LR}\n" +
                        "    bl   m%08X             // E8h %02Xh %02Xh = CALL %+d (m%08X)\n" +
                        "    pop  {LR}"
                        , pointsTo, b1, b2 & 0xFF, signedInt, pointsTo
                );
                break;
            }
            case 0xE9: // JMP rel16/32 | (2 байта в 16-битном режиме, 4 байта в 32/64-битном)
                bytes = 3;
                final int b1 = bin.get(offset + 1) & 0xFF;
                final int b2 = bin.get(offset + 2);
                final int signedInt = (b2 << 8) | b1;
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedInt;
                if (pointsTo < 0) {
                    pointsTo += 0x10000;
                }
                m33candidate = String.format("    B m%08X              // E9h %02Xh %02Xh = JMP %+d (m%08X)", pointsTo, b1, b2 & 0xFF, signedInt, pointsTo);
                nextOffset = -1; // no direct pass
                break;
            case 0xEB: // JMP near
                bytes = 2;
                final byte signedByte = bin.get(offset + 1); // relative jump to (signed byte)
                nextOffset = offset + bytes;
                pointsTo = nextOffset + signedByte;
                m33candidate = String.format("    B m%08X               // EBh %02Xh = JMP %+d (m%08X)", pointsTo, signedByte, signedByte, pointsTo);
                nextOffset = -1; // no direct pass
                break;
            case 0xF8: { // CLC
                changesCF = true;
                m33candidate = "    MOV  R0, #0\n" +
                        "    LSRS R0, R0, #1    // CLC";
                nextOffset = offset + 1;
                break;
            }
            case 0xF9: { // STC — Set Carry Flag
                changesCF = true;
                m33candidate = "    MOV  R0, #1\n" +
                        "    LSRS R0, R0, #1    // STC";
                nextOffset = offset + 1;
                break;
            }
            default:
                // just W/A - remove it
                changesCF = true;
                changesPF = true;
                changesAF = true;
                passed = true;
                m33candidate = String.format("TODO: // %02Xh", keyByte);
                break;
        }
    }

    @Override
    public String toString() {
        String res = "";
        if(need_recover) {
            res += String.format("m%08X_P:\n", offset);
            res += "    MSR APSR_nzcvq, R0        // восстановить только N,Z,C,V (после сохранения для PF)\n";
        }
        if(pointedFrom.size() > 1) {
            res += String.format("m%08X:\n", offset);
        }
        res += String.format("    // x86 %08X; opcode: %02X; bytes: %d\n", offset, keyByte, bytes);
        if (passed) {
            return res + m33candidate;
        }
        return res + String.format("// %08X: %02Xh", offset, keyByte);
    }

}
