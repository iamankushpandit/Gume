#!/usr/bin/env python3
"""Read the real flash and RAM figures out of a built firmware ELF.

README.md and CLAUDE.md both quote the size line from `pio run`, and both went
404 bytes stale anyway -- because the only check on them compared the two
documents against *each other*, never against a build. Two documents can agree
perfectly and both be wrong, which is exactly what happened.

This reads the numbers from `.pio/build/<env>/firmware.elf` instead, so the
documented figures can be checked against something the compiler produced.

The sums mirror what the espressif32 platform prints, and were pinned against
a real `pio run` on this project:

    Flash = .flash.text + .flash.rodata + .iram0.vectors + .iram0.text
            + .dram0.data                                   (= 2,347,573)
    RAM   = .dram0.data + .dram0.bss                        (=    72,524)

`.flash.appdesc` is deliberately *not* counted -- including it overshoots the
reported figure by exactly its own 256 bytes.

The ELF section headers are parsed directly rather than shelling out to
`xtensa-esp32-elf-size`, so this works on a checkout where the toolchain is not
on PATH, and costs nothing when there is no build to read.
"""

import os
import struct

FLASH_SECTIONS = (".flash.text", ".flash.rodata", ".iram0.vectors",
                  ".iram0.text", ".dram0.data")
RAM_SECTIONS = (".dram0.data", ".dram0.bss")

FLASH_CAPACITY = 3145728        # huge_app.csv, the 3 MB app partition
RAM_CAPACITY = 327680


def section_sizes(elf_path):
    """Map section name -> size, for every allocatable section in the ELF."""
    with open(elf_path, "rb") as handle:
        blob = handle.read()

    if blob[:4] != b"\x7fELF":
        raise ValueError("%s is not an ELF file" % elf_path)

    is64 = blob[4] == 2
    endian = "<" if blob[5] == 1 else ">"

    if is64:
        sh_off = struct.unpack_from(endian + "Q", blob, 0x28)[0]
        sh_entsize, sh_num, sh_strndx = struct.unpack_from(endian + "HHH", blob, 0x3A)
        fmt, name_at, size_at, flags_at = endian + "IIQQQQ", 0, 5, 2
    else:
        sh_off = struct.unpack_from(endian + "I", blob, 0x20)[0]
        sh_entsize, sh_num, sh_strndx = struct.unpack_from(endian + "HHH", blob, 0x2E)
        fmt, name_at, size_at, flags_at = endian + "IIIIII", 0, 5, 2

    headers = [struct.unpack_from(fmt, blob, sh_off + i * sh_entsize)
               for i in range(sh_num)]

    strtab_header = headers[sh_strndx]
    strtab = strtab_header[4]           # sh_offset

    def name_of(offset):
        end = blob.index(b"\0", strtab + offset)
        return blob[strtab + offset:end].decode("utf-8", "replace")

    sizes = {}
    for header in headers:
        if not header[flags_at] & 0x2:  # SHF_ALLOC
            continue
        size = header[size_at]
        if size:
            sizes[name_of(header[name_at])] = size
    return sizes


def build_figures(elf_path):
    """(flash_bytes, ram_bytes) for a built ELF, or None if it is not there.

    Returning None rather than raising is the point: a fresh checkout has never
    been built, and a docs check must stay clean on one.
    """
    if not os.path.isfile(elf_path):
        return None
    sizes = section_sizes(elf_path)
    if not any(name in sizes for name in FLASH_SECTIONS):
        return None                     # not a firmware ELF we recognise
    flash = sum(sizes.get(name, 0) for name in FLASH_SECTIONS)
    ram = sum(sizes.get(name, 0) for name in RAM_SECTIONS)
    return flash, ram


def default_elf(root, env="app"):
    return os.path.join(root, ".pio", "build", env, "firmware.elf")


if __name__ == "__main__":
    import sys

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    figures = build_figures(default_elf(root))
    if figures is None:
        print("No build found at %s -- run `pio run` first."
              % default_elf(root))
        sys.exit(0)
    flash, ram = figures
    print("Flash: %s / %s bytes (%.1f%%)"
          % (format(flash, ","), format(FLASH_CAPACITY, ","),
             100.0 * flash / FLASH_CAPACITY))
    print("RAM:   %s / %s bytes (%.1f%%)"
          % (format(ram, ","), format(RAM_CAPACITY, ","),
             100.0 * ram / RAM_CAPACITY))
