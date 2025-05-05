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

            if (DEBUG) System.out.println(String.format("m%08X", offset) + (m33i.codeTraced ? "!\n" : ">\n") + m33i.m33candidate);

            if (m33i.codeTraced) { // already passed
                if (offsets.isEmpty()) {
                    break;
                }
                offset = offsets.pollFirst();
                continue;
            }
            m33i.codeTraced = true;
            if (m33i.pointsTo >= 0) {
                if (m33i.pointsTo < m33.size()) {
                    m33.get(m33i.pointsTo).pointedFrom.add(offset);
                    if (m33i.PFRecoveringRequest) m33.get(m33i.pointsTo).recoverFlagsAfterPFTest = true;
                    offsets.push(m33i.pointsTo);
                } else {
                    System.out.println("Skipping " + m33i.pointsTo);
                }
            }
            offset = m33i.nextOffset;
        }
        // TODO: detect it better
        m33.add(new M33Replacement(offset, 0x90, "    NOP   // предположительно, конец программы"));
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
    }

    private static void readBinary(FileInputStream input) throws IOException {
        int byteData;
        while ((byteData = input.read()) != -1) {
            bin.add((byte) byteData);
        }
    }

}
