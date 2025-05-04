import java.io.FileInputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedList;

public class Main {

    final static ArrayList<Byte> bin = new ArrayList<>();
    final static ArrayList<M33Replacement> m33 = new ArrayList<>();
    final static boolean DEBUG = false;

    public static void main(String[] args) {
        try (final FileInputStream input = new FileInputStream(args[0])) {
            readBinary(input);
            initialParsing();
            traceForward();
            backTrace();
            outM33();
        } catch (IOException e) {
            System.err.println("Error reading file: " + e.getMessage());
        }
    }

    private static void outM33() {
        System.out.println("start_it:");
        for (int off = 0; off < m33.size(); ) {
            final var m33i = m33.get(off);
            m33i.regenerate(bin);
            System.out.println(m33i);
            off += m33i.bytes;
        }
    }

    private static void traceForward() {
        int entryPoint = 0; // TODO: detect by file
        final var offsets = new LinkedList<Integer>();
        int offset = entryPoint;
        for ( ; ; ) {
            if (offset < 0 || offset >= m33.size()) { // JMP case, or list finished
                if (offsets.isEmpty()) {
                    break;
                }
                offset = offsets.pollFirst();
                continue;
            }
            final var m33i = m33.get(offset);

            if (DEBUG) System.out.println(String.format("m%08X", offset) + (m33i.passed ? "!\n" : ">\n") + m33i.m33candidate);

            if (m33i.passed) { // already passed
                if (offsets.isEmpty()) {
                    break;
                }
                offset = offsets.pollFirst();
                continue;
            }
            m33i.passed = true;
            if (m33i.pointsTo >= 0) {
                if (m33i.pointsTo < m33.size()) {
                    m33.get(m33i.pointsTo).pointedFrom.add(offset);
                    if (m33i.PFback) m33.get(m33i.pointsTo).need_recover = true;
                    offsets.push(m33i.pointsTo);
                } else {
                    System.out.println("Skipping " + m33i.pointsTo);
                }
            }
            offset = m33i.nextOffset;
        }
    }

    private static void markCFisRequired(int offset) {
        final var offsets = new LinkedList<Integer>();
        for ( ; ; ) {
            if (offset < 0 || offset >= m33.size()) { // JMP case, or list finished
                if (offsets.isEmpty()) {
                    break;
                }
                offset = offsets.pollFirst();
                continue;
            }
            final var m33i = m33.get(offset);
            if (!m33i.CFrequired && !m33i.changesCF) {
                m33i.CFrequired = true;
                if (m33i.pointsTo >= 0) {
                    if (m33i.pointsTo < m33.size()) {
                        offsets.push(m33i.pointsTo);
                    }
                }
            }
            offset = m33i.nextOffset;
        }
    }

    private static void markPFisRequired(int offset) {
        final var offsets = new LinkedList<Integer>();
        for ( ; ; ) {
            if (offset < 0 || offset >= m33.size()) { // JMP case, or list finished
                if (offsets.isEmpty()) {
                    break;
                }
                offset = offsets.pollFirst();
                continue;
            }
            final var m33i = m33.get(offset);
            if (!m33i.PFrequired && !m33i.changesPF) {
                m33i.PFrequired = true;
                if (m33i.pointsTo >= 0) {
                    if (m33i.pointsTo < m33.size()) {
                        offsets.push(m33i.pointsTo);
                    }
                }
            }
            offset = m33i.nextOffset;
        }
    }

    private static void markAFisRequired(int offset) {
        final var offsets = new LinkedList<Integer>();
        for ( ; ; ) {
            if (offset < 0 || offset >= m33.size()) { // JMP case, or list finished
                if (offsets.isEmpty()) {
                    break;
                }
                offset = offsets.pollFirst();
                continue;
            }
            final var m33i = m33.get(offset);
            if (!m33i.AFrequired && !m33i.changesAF) {
                m33i.AFrequired = true;
                if (m33i.pointsTo >= 0) {
                    if (m33i.pointsTo < m33.size()) {
                        offsets.push(m33i.pointsTo);
                    }
                }
            }
            offset = m33i.nextOffset;
        }
    }

    private static void initialParsing() {
        int offset = 0;
        int prevOffset = -1;
        for (; offset < bin.size(); ) {
            final var mi = new M33Replacement(offset, prevOffset, bin);
            m33.add(mi);
            prevOffset = offset;
            if (mi.bytes > 1) {  // W/A TODO: cleanup it
                for (int i = 1; i < mi.bytes; ++i) {
                    m33.add(new M33Replacement(offset + i, prevOffset, bin));
                }
            }
            offset += mi.bytes;
        }
        m33.add(new M33Replacement(offset, 0x90, "    NOP"));
    }

    private static void readBinary(FileInputStream input) throws IOException {
        int byteData;
        while ((byteData = input.read()) != -1) {
            bin.add((byte) byteData);
        }
    }

    private static void backTrace() {
        for (int off = 0; off < m33.size(); off++) {
            final var m33i = m33.get(off);
            if (!m33i.passed) {
                continue; // not tracked forward code should not be tracked back
            }
            if (m33i.AFback) {
                if (DEBUG) System.out.println(m33i + "\n ^^^ said to track back for AF");
                backTraceL2A(m33i);
            }
            if (m33i.PFback) {
                if (DEBUG) System.out.println(m33i + "\n ^^^ said to track back for PF");
                backTraceL2P(m33i);
            }
            if (m33i.CFback) {
                if (DEBUG) System.out.println(m33i + "\n ^^^ said to track back for CF");
                backTraceL2C(m33i);
            }
        }
    }

    private static void backTraceL2A(M33Replacement m33i) {
        for (var fromOff : m33i.pointedFrom) {
            if (fromOff < 0) {
                if (DEBUG) System.out.println("m33i.pointedFrom < 0 ");
                continue;
            }
            if (fromOff == m33i.offset) {
                if (DEBUG) System.out.println("m33i.offset == m33i.pointedFrom");
                continue;
            }
            var m33f = m33.get(fromOff);
            if (m33f.AFrequired) {
                if (DEBUG) System.out.println(m33f + "\n ^^^ already processed with backtrace");
                continue;
            }
            if (m33i.AFback && m33f.changesAF) {
                if (DEBUG) System.out.println(m33f + "\n ^^^ said changesAF, so this branch is finished");
                markAFisRequired(m33f.offset);
                continue;
            }
            m33f.AFback = m33i.AFback;
            if (DEBUG) System.out.println(m33f + "\n ^^^ said it is just pass AF though");
            backTraceL2A(m33f);
        }
    }
    private static void backTraceL2P(M33Replacement m33i) {
        for (var fromOff : m33i.pointedFrom) {
            if (fromOff < 0) {
                if (DEBUG) System.out.println("m33i.pointedFrom < 0 ");
                continue;
            }
            if (fromOff == m33i.offset) {
                if (DEBUG) System.out.println("m33i.offset == m33i.pointedFrom");
                continue;
            }
            var m33f = m33.get(fromOff);
            if (m33f.PFrequired) {
                if (DEBUG) System.out.println(m33f + "\n ^^^ already processed with backtrace");
                continue;
            }
            if (m33i.PFback && m33f.changesPF) {
                if (DEBUG) System.out.println(m33f + "\n ^^^ said changesCP, so this branch is finished");
                markPFisRequired(m33f.offset);
                continue;
            }
            m33f.PFback = m33i.PFback;
            if (DEBUG) System.out.println(m33f + "\n ^^^ said it is just pass PF though");
            backTraceL2P(m33f);
        }
    }
    private static void backTraceL2C(M33Replacement m33i) {
        int i = 0;
        for (var fromOff : m33i.pointedFrom) {
            if (m33i.pointedFrom.size() > 1) {
                if (DEBUG) System.out.printf(
                        "Branch #%d/%d started at %08X is pointed from %08X\n",
                        ++i, m33i.pointedFrom.size(), m33i.offset, fromOff
                );
            }
            if (fromOff < 0) {
                if (DEBUG) System.out.println("m33i.pointedFrom < 0 ");
                continue;
            }
            if (fromOff == m33i.offset) {
                if (DEBUG) System.out.println("m33i.offset == m33i.pointedFrom");
                continue;
            }
            var m33f = m33.get(fromOff);
            if (m33f.CFrequired) {
                if (DEBUG) System.out.println(m33f + "\n ^^^ already processed with backtrace");
                continue;
            }
            if (m33i.CFback && m33f.changesCF) {
                if (DEBUG) System.out.println(m33f + "\n ^^^ said changesCF, so this branch is finished");
                markCFisRequired(m33f.offset);
                continue;
            }
            m33f.CFback = m33i.CFback;
            if (DEBUG) System.out.println(m33f + "\n ^^^ said it is just pass CF though");
            backTraceL2C(m33f);
        }
    }

}
