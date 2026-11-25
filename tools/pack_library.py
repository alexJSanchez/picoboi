#!/usr/bin/env python3
"""Pack Game Boy ROMs into a flashable UF2 for the PicoBoy library.

    pack_library.py rom1.gb rom2.gb ... -o library.uf2

The UF2 covers ONLY the library index sector and the ROM area (see docs/FLASH_LAYOUT.md),
so dragging it onto the RPI-RP2 drive leaves the firmware and the saves alone. [VERIFY on
hardware before trusting it with a real save.]

All flash addresses come from storage/flash_layout.h (the single source of truth); the
index format is described in docs/FLASH_LAYOUT.md and implemented on the firmware side in
storage/rom_library.c. Never commit a ROM or a UF2 that contains one.
"""
import argparse
import glob
import pathlib
import re
import struct
import sys
import zlib

# ---- index format v1 (must match storage/rom_library.h; checked by the golden-file tests) ----
MAGIC = b"PBLB"
VERSION = 1
HEADER_SIZE = 16
ENTRY_SIZE = 48
NAME_LEN = 24

FLAG_UNSUPPORTED_MAPPER = 0x0001  # the emulator core cannot run this cartridge type
FLAG_CGB_ONLY = 0x0002            # Game Boy Color only; this build is DMG only
FLAG_BAD_HEADER = 0x0004          # header checksum wrong or header values the core cannot use
FLAG_BAD_SIZE = 0x0008            # file size does not match the size code in the header
FLAG_CGB_ENHANCED = 0x0010        # informational: also has colour features, runs in DMG mode
BLOCKING_FLAGS = FLAG_UNSUPPORTED_MAPPER | FLAG_CGB_ONLY | FLAG_BAD_HEADER | FLAG_BAD_SIZE

# Cartridge type (header byte 0x147) -> mapper number, -1 = not supported. Copied from
# Peanut-GB v1.3.0 gb_init() (third_party/peanut_gb/peanut_gb.h); the core stays the
# authority, this table only lets the PC tool and the launcher say so early.
CART_MBC = [0, 1, 1, 1, -1, 2, 2, -1, 0, 0, -1, 0, 0, 0, -1, 3,
            3, 3, 3, 3, -1, -1, -1, -1, -1, 5, 5, 5, 5, 5, 5, -1]
# Save RAM bytes by RAM size code (header 0x149), as Peanut-GB's gb_get_save_size(); MBC2 is 512.
RAM_BYTES = [0, 0x800, 0x2000, 0x8000, 0x20000]

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY = 0x00002000
UF2_FAMILY_RP2040 = 0xE48BFF56
UF2_PAYLOAD = 256


class PackError(Exception):
    pass


def expand_inputs(args):
    """Turns the ROM arguments into a list of file paths.

    Windows shells (PowerShell, cmd) do not expand wildcards, so we do it here. A folder means
    every .gb file in it; an argument with * ? or [ is a pattern; anything else is one file.
    Arguments keep the order given; each expansion is sorted by name.
    """
    paths = []
    for a in args:
        p = pathlib.Path(a)
        if p.is_dir():
            found = sorted(q for q in p.iterdir() if q.is_file() and q.suffix.lower() == ".gb")
            if not found:
                raise PackError(f"{a}: no .gb files in this folder")
            paths.extend(found)
        elif any(c in a for c in "*?["):
            found = sorted(pathlib.Path(m) for m in glob.glob(a) if pathlib.Path(m).is_file())
            if not found:
                raise PackError(f"{a}: no files match")
            paths.extend(found)
        else:
            paths.append(p)
    return paths


# ---------------------------------------------------------------------------------------
# Layout: read the constants from storage/flash_layout.h
# ---------------------------------------------------------------------------------------
def load_layout(header_path):
    text = pathlib.Path(header_path).read_text(encoding="utf-8")
    defs = dict(re.findall(r"#define\s+(PB_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|[0-9]+)u\b", text))
    want = ["PB_FLASH_XIP_BASE", "PB_FLASH_SECTOR_SIZE", "PB_LIB_INDEX_OFFSET", "PB_LIB_INDEX_SIZE",
            "PB_ROM_AREA_OFFSET", "PB_ROM_AREA_END"]
    missing = [k for k in want if k not in defs]
    if missing:
        raise PackError(f"{header_path}: cannot find {', '.join(missing)}")
    return {k: int(defs[k], 0) for k in want}


# ---------------------------------------------------------------------------------------
# ROM analysis
# ---------------------------------------------------------------------------------------
def header_checksum_ok(rom):
    x = 0
    for i in range(0x134, 0x14D):
        x = (x - rom[i] - 1) & 0xFF
    return x == rom[0x14D]


def rom_title(rom, fallback):
    cgb = rom[0x143]
    end = 0x143 if cgb in (0x80, 0xC0) else 0x144  # newer headers use 15 title bytes
    raw = bytes(b for b in rom[0x134:end] if 0x20 <= b <= 0x7E)
    title = raw.decode("ascii").strip()
    if not title:
        title = re.sub(r"[^\x20-\x7E]", "?", fallback).strip() or "ROM"
    return title[:NAME_LEN - 1]


def analyse_rom(rom, fallback_name):
    """Returns a dict with everything the index needs. Raises PackError if it is not a ROM at all."""
    if len(rom) < 0x150:
        raise PackError(f"{fallback_name}: too small to be a Game Boy ROM ({len(rom)} bytes)")
    cart_type = rom[0x147]
    size_code = rom[0x148]
    ram_code = rom[0x149]
    cgb_flag = rom[0x143]
    flags = 0

    if not header_checksum_ok(rom):
        flags |= FLAG_BAD_HEADER
    mapper = CART_MBC[cart_type] if cart_type < len(CART_MBC) else -1
    if mapper < 0:
        flags |= FLAG_UNSUPPORTED_MAPPER
    if cgb_flag == 0xC0:
        flags |= FLAG_CGB_ONLY
    elif cgb_flag == 0x80:
        flags |= FLAG_CGB_ENHANCED

    if size_code > 8:
        flags |= FLAG_BAD_HEADER
        banks = 0
        expected = None
    else:
        banks = 2 << size_code
        expected = banks * 0x4000
        if len(rom) != expected:
            flags |= FLAG_BAD_SIZE

    if mapper == 2:
        ram_size = 0x200
    elif ram_code < len(RAM_BYTES):
        ram_size = RAM_BYTES[ram_code]
    else:
        ram_size = 0
        flags |= FLAG_BAD_HEADER  # RAM size code the core cannot handle

    return {
        "name": rom_title(rom, fallback_name),
        "size": len(rom),
        "crc": zlib.crc32(rom) & 0xFFFFFFFF,
        "ram_size": ram_size,
        "rom_banks": banks,
        "flags": flags,
        "mapper": mapper & 0xFF,  # 0xFF = unsupported
        "cart_type": cart_type,
    }


# ---------------------------------------------------------------------------------------
# Index and UF2
# ---------------------------------------------------------------------------------------
def build_index(entries, layout):
    """entries: list of dicts from analyse_rom() plus 'offset'. Returns the index sector bytes."""
    max_entries = (layout["PB_LIB_INDEX_SIZE"] - HEADER_SIZE) // ENTRY_SIZE
    if len(entries) > max_entries:
        raise PackError(f"too many ROMs: {len(entries)} (the index holds {max_entries})")
    body = b""
    for e in entries:
        name = e["name"].encode("ascii")
        if len(name) >= NAME_LEN:
            raise PackError(f"name too long: {e['name']!r}")
        body += struct.pack("<24sIIIIHHBB2s", name, e["offset"], e["size"], e["crc"], e["ram_size"],
                            e["rom_banks"], e["flags"], e["mapper"], e["cart_type"], b"\0\0")
    assert len(body) == ENTRY_SIZE * len(entries)
    header = struct.pack("<4sHHHHI", MAGIC, VERSION, len(entries), ENTRY_SIZE, 0,
                         zlib.crc32(body) & 0xFFFFFFFF)
    assert len(header) == HEADER_SIZE
    sector = header + body
    return sector + b"\xFF" * (layout["PB_LIB_INDEX_SIZE"] - len(sector))


def align_up(n, a):
    return (n + a - 1) // a * a


def plan_library(roms, layout):
    """roms: list of (fallback_name, bytes). Returns (entries, area_bytes) with offsets assigned."""
    sector = layout["PB_FLASH_SECTOR_SIZE"]
    offset = layout["PB_ROM_AREA_OFFSET"]
    area_end = layout["PB_ROM_AREA_END"]
    entries = []
    area = bytearray()
    for name, data in roms:
        e = analyse_rom(data, name)
        e["offset"] = offset
        alloc = align_up(len(data), sector)
        if offset + alloc > area_end:
            used = offset - layout["PB_ROM_AREA_OFFSET"]
            raise PackError(f"ROM area full: {name} needs {alloc} bytes, only "
                            f"{area_end - offset} left (of {area_end - layout['PB_ROM_AREA_OFFSET']}; "
                            f"{used} used)")
        area += data + b"\xFF" * (alloc - len(data))  # whole sectors, so none is left half old
        offset += alloc
        entries.append(e)
    return entries, bytes(area)


def build_uf2(start_flash_offset, data, layout):
    """One UF2 file with `data` at flash offset `start_flash_offset` (sector aligned, whole sectors)."""
    if start_flash_offset % layout["PB_FLASH_SECTOR_SIZE"] or len(data) % layout["PB_FLASH_SECTOR_SIZE"]:
        raise PackError("UF2 payload must cover whole flash sectors")
    nblocks = len(data) // UF2_PAYLOAD
    out = bytearray()
    for i in range(nblocks):
        chunk = data[i * UF2_PAYLOAD:(i + 1) * UF2_PAYLOAD]
        addr = layout["PB_FLASH_XIP_BASE"] + start_flash_offset + i * UF2_PAYLOAD
        block = struct.pack("<IIIIIIII", UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_FLAG_FAMILY, addr,
                            UF2_PAYLOAD, i, nblocks, UF2_FAMILY_RP2040)
        block += chunk + b"\0" * (476 - UF2_PAYLOAD) + struct.pack("<I", UF2_MAGIC_END)
        assert len(block) == 512
        out += block
    return bytes(out)


def pack(roms, layout):
    """Returns (index_sector, uf2_bytes, entries)."""
    entries, area = plan_library(roms, layout)
    index = build_index(entries, layout)
    assert layout["PB_LIB_INDEX_OFFSET"] + layout["PB_LIB_INDEX_SIZE"] == layout["PB_ROM_AREA_OFFSET"]
    uf2 = build_uf2(layout["PB_LIB_INDEX_OFFSET"], index + area, layout)
    return index, uf2, entries


# ---------------------------------------------------------------------------------------
def describe_flags(flags):
    names = []
    if flags & FLAG_UNSUPPORTED_MAPPER: names.append("unsupported cartridge type")
    if flags & FLAG_CGB_ONLY: names.append("Game Boy Color only")
    if flags & FLAG_BAD_HEADER: names.append("bad header")
    if flags & FLAG_BAD_SIZE: names.append("size does not match header")
    if flags & FLAG_CGB_ENHANCED: names.append("colour-enhanced (runs as DMG)")
    return ", ".join(names)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Pack Game Boy ROMs into a PicoBoy library UF2.")
    ap.add_argument("roms", nargs="+",
                    help=".gb files, a folder (all its .gb files) or a pattern such as roms/*.gb, "
                         "in the order they should appear")
    ap.add_argument("-o", "--output", default="library.uf2", help="UF2 file to write")
    ap.add_argument("--layout", default=str(pathlib.Path(__file__).resolve().parent.parent / "storage" / "flash_layout.h"),
                    help="path of flash_layout.h (default: the one in this repository)")
    ap.add_argument("--dump-index", metavar="FILE", help="also write the raw 4096-byte index sector")
    args = ap.parse_args(argv)

    try:
        layout = load_layout(args.layout)
        roms = []
        for path in expand_inputs(args.roms):
            roms.append((path.stem, path.read_bytes()))
        index, uf2, entries = pack(roms, layout)
    except (PackError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    pathlib.Path(args.output).write_bytes(uf2)
    if args.dump_index:
        pathlib.Path(args.dump_index).write_bytes(index)
    print(f"{'#':>2}  {'name':<23} {'size':>8}  {'flash offset':>12}  mapper  RAM   crc32")
    for i, e in enumerate(entries):
        mapper = "none" if e["mapper"] == 0 else ("?" if e["mapper"] == 0xFF else f"MBC{e['mapper']}")
        print(f"{i:>2}  {e['name']:<23} {e['size']:>8}  0x{e['offset']:08X}  {mapper:<6}  "
              f"{e['ram_size'] // 1024:>2}K   {e['crc']:08x}")
        if e["flags"] & (BLOCKING_FLAGS | FLAG_CGB_ENHANCED):
            note = "cannot run" if e["flags"] & BLOCKING_FLAGS else "note"
            print(f"      {note}: {describe_flags(e['flags'])}")
    used = sum(align_up(e["size"], layout["PB_FLASH_SECTOR_SIZE"]) for e in entries)
    total = layout["PB_ROM_AREA_END"] - layout["PB_ROM_AREA_OFFSET"]
    print(f"ROM area: {used} of {total} bytes used. Wrote {args.output} ({len(uf2)} bytes).")
    print("Hold X while powering on, then copy the UF2 onto the RPI-RP2 drive. Do not share it if it "
          "contains ROMs you do not have the right to distribute.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
