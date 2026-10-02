#!/usr/bin/env python3
import struct
import sys
from pathlib import Path

PT_GNU_STACK = 0x6474E551
PF_X = 0x1


def clear_execstack(path: Path) -> bool:
    data = bytearray(path.read_bytes())
    if data[:4] != b"\x7fELF":
        raise SystemExit(f"not an ELF file: {path}")
    elf_class = data[4]
    endian = data[5]
    if endian != 1:
        raise SystemExit("only little-endian ELF is supported")

    if elf_class == 2:
        e_phoff = struct.unpack_from("<Q", data, 32)[0]
        e_phentsize = struct.unpack_from("<H", data, 54)[0]
        e_phnum = struct.unpack_from("<H", data, 56)[0]
        flags_offset = 4
    elif elf_class == 1:
        e_phoff = struct.unpack_from("<I", data, 28)[0]
        e_phentsize = struct.unpack_from("<H", data, 42)[0]
        e_phnum = struct.unpack_from("<H", data, 44)[0]
        flags_offset = 24
    else:
        raise SystemExit("unsupported ELF class")

    changed = False
    for index in range(e_phnum):
        off = e_phoff + index * e_phentsize
        p_type = struct.unpack_from("<I", data, off)[0]
        if p_type != PT_GNU_STACK:
            continue
        flags_pos = off + flags_offset
        flags = struct.unpack_from("<I", data, flags_pos)[0]
        if flags & PF_X:
            struct.pack_into("<I", data, flags_pos, flags & ~PF_X)
            changed = True

    if changed:
        path.write_bytes(data)
    return changed


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} <elf-file>")
    target = Path(sys.argv[1])
    print("changed" if clear_execstack(target) else "already-clear")
