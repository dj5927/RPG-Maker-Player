package com.dj5927.rpgmakerplayer;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.InflaterInputStream;

/** Minimal Ruby Marshal 4.8 reader for RPG Maker Scripts.rxdata/rvdata/rvdata2. */
final class RubyScriptsArchive {
    private RubyScriptsArchive() {}

    static int extract(File archive, File outDir) throws Exception {
        byte[] data;
        try (FileInputStream in = new FileInputStream(archive);
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) >= 0) out.write(buffer, 0, read);
            data = out.toByteArray();
        }

        Object root = new MarshalReader(data).readRoot();
        if (!(root instanceof List)) throw new IOException("Scripts root is not Array");
        List<?> scripts = (List<?>) root;
        int extracted = 0;
        for (int i = 0; i < scripts.size(); i++) {
            Object item = scripts.get(i);
            if (!(item instanceof List)) continue;
            List<?> entry = (List<?>) item;
            if (entry.size() < 3 || !(entry.get(2) instanceof byte[])) continue;
            byte[] packed = (byte[]) entry.get(2);
            byte[] source = inflate(packed);
            File target = new File(outDir, String.format(java.util.Locale.ROOT, "%05d.rb", i));
            try (FileOutputStream out = new FileOutputStream(target, false)) {
                out.write(source);
                out.write('\n');
            }
            extracted++;
        }
        if (extracted == 0) throw new IOException("Scripts sections not found");
        return extracted;
    }

    private static byte[] inflate(byte[] packed) throws IOException {
        try (InflaterInputStream in = new InflaterInputStream(new ByteArrayInputStream(packed));
             ByteArrayOutputStream out = new ByteArrayOutputStream(Math.max(1024, packed.length * 2))) {
            byte[] buffer = new byte[16 * 1024];
            int read;
            while ((read = in.read(buffer)) >= 0) out.write(buffer, 0, read);
            return out.toByteArray();
        }
    }

    private static final class MarshalReader {
        private final byte[] data;
        private int pos;
        private final ArrayList<Object> objects = new ArrayList<>();
        private final ArrayList<String> symbols = new ArrayList<>();

        MarshalReader(byte[] data) { this.data = data; }

        Object readRoot() throws Exception {
            if (readU8() != 4 || readU8() != 8) throw new IOException("Unsupported Ruby Marshal version");
            return readObject();
        }

        private Object readObject() throws Exception {
            int type = readU8();
            switch (type) {
                case '0': return null;
                case 'T': return Boolean.TRUE;
                case 'F': return Boolean.FALSE;
                case 'i': return readFixnum();
                case '[': {
                    int count = checkedCount(readFixnum());
                    ArrayList<Object> list = new ArrayList<>(count);
                    objects.add(list);
                    for (int i = 0; i < count; i++) list.add(readObject());
                    return list;
                }
                case '"': {
                    int length = checkedCount(readFixnum());
                    byte[] bytes = readBytes(length);
                    objects.add(bytes);
                    return bytes;
                }
                case ':': {
                    int length = checkedCount(readFixnum());
                    String symbol = new String(readBytes(length), java.nio.charset.StandardCharsets.ISO_8859_1);
                    symbols.add(symbol);
                    return symbol;
                }
                case ';': {
                    int index = readFixnum();
                    if (index < 0 || index >= symbols.size()) throw new IOException("Bad symbol link " + index);
                    return symbols.get(index);
                }
                case '@': {
                    int index = readFixnum();
                    if (index < 0 || index >= objects.size()) throw new IOException("Bad object link " + index);
                    return objects.get(index);
                }
                case 'I': {
                    Object value = readObject();
                    int ivars = checkedCount(readFixnum());
                    for (int i = 0; i < ivars; i++) {
                        readObject();
                        readObject();
                    }
                    return value;
                }
                default:
                    throw new IOException("Unsupported Marshal type 0x" + Integer.toHexString(type) +
                            " at " + (pos - 1));
            }
        }

        private int checkedCount(int value) throws IOException {
            if (value < 0 || value > 2_000_000) throw new IOException("Invalid Marshal length " + value);
            return value;
        }

        private int readFixnum() throws IOException {
            int raw = readU8();
            int c = raw >= 128 ? raw - 256 : raw;
            if (c == 0) return 0;
            if (c > 4) return c - 5;
            if (c < -4) return c + 5;
            if (c > 0) {
                int value = 0;
                for (int i = 0; i < c; i++) value |= readU8() << (8 * i);
                return value;
            }
            int count = -c;
            int value = -1;
            for (int i = 0; i < count; i++) {
                value &= ~(0xff << (8 * i));
                value |= readU8() << (8 * i);
            }
            return value;
        }

        private int readU8() throws IOException {
            if (pos >= data.length) throw new EOFException("Unexpected end of Marshal data");
            return data[pos++] & 0xff;
        }

        private byte[] readBytes(int length) throws IOException {
            if (length < 0 || pos + length > data.length) throw new EOFException("Marshal string truncated");
            byte[] out = new byte[length];
            System.arraycopy(data, pos, out, 0, length);
            pos += length;
            return out;
        }
    }
}
