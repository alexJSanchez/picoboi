"""Tests for tools/pack_library.py. Run with ctest, or:  python tests/test_pack_library.py

Set PB_UPDATE_GOLDEN=1 to rewrite tests/golden/index_v1.bin after an intentional format change
(then also update the expected values in tests/test_rom_library.c).
"""
import importlib.util
import os
import pathlib
import struct
import sys
import unittest
import zlib

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import synthetic_roms as syn  # noqa: E402

spec = importlib.util.spec_from_file_location("pack_library", ROOT / "tools" / "pack_library.py")
pl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pl)

GOLDEN = HERE / "golden" / "index_v1.bin"
LAYOUT = pl.load_layout(ROOT / "storage" / "flash_layout.h")


def parse_uf2(data):
    assert len(data) % 512 == 0
    blocks = []
    for i in range(0, len(data), 512):
        m0, m1, flags, addr, size, no, total, fam = struct.unpack("<IIIIIIII", data[i:i + 32])
        end = struct.unpack("<I", data[i + 508:i + 512])[0]
        blocks.append((m0, m1, flags, addr, size, no, total, fam, end, data[i + 32:i + 32 + size]))
    return blocks


class PackLibraryTests(unittest.TestCase):
    def test_crc32_check_value(self):
        self.assertEqual(zlib.crc32(b"123456789") & 0xFFFFFFFF, 0xCBF43926)

    def test_layout_is_read_from_the_header(self):
        self.assertEqual(LAYOUT["PB_FLASH_XIP_BASE"], 0x10000000)
        self.assertEqual(LAYOUT["PB_FLASH_SECTOR_SIZE"], 4096)
        self.assertEqual(LAYOUT["PB_LIB_INDEX_OFFSET"], 0x100000)
        self.assertEqual(LAYOUT["PB_ROM_AREA_OFFSET"], 0x101000)
        self.assertEqual(LAYOUT["PB_ROM_AREA_END"], 0xA00000)

    def test_golden_index(self):
        index, _, _ = pl.pack(syn.GOLDEN_ROMS, LAYOUT)
        self.assertEqual(len(index), 4096)
        if os.environ.get("PB_UPDATE_GOLDEN") == "1":
            GOLDEN.parent.mkdir(exist_ok=True)
            GOLDEN.write_bytes(index)
        self.assertEqual(index, GOLDEN.read_bytes(),
                         "index format changed; run with PB_UPDATE_GOLDEN=1 if intentional")

    def test_entries_and_flags(self):
        _, _, entries = pl.pack(syn.GOLDEN_ROMS, LAYOUT)
        by = {e["name"]: e for e in entries}
        self.assertEqual(by["TONE-ONLY"]["flags"], 0)
        self.assertEqual(by["TONE-ONLY"]["mapper"], 0)
        self.assertEqual(by["MBC1 GAME"]["mapper"], 1)
        self.assertEqual(by["MBC1 GAME"]["ram_size"], 0x2000)
        self.assertEqual(by["BIG MBC3"]["mapper"], 3)
        self.assertEqual(by["BIG MBC3"]["ram_size"], 0x8000)
        self.assertEqual(by["BIG MBC3"]["rom_banks"], 64)
        self.assertEqual(by["MBC2 THING"]["mapper"], 2)
        self.assertEqual(by["MBC2 THING"]["ram_size"], 0x200)
        self.assertEqual(by["CGB ONLY"]["flags"], pl.FLAG_CGB_ONLY)
        self.assertEqual(by["CAMERA"]["mapper"], 0xFF)
        self.assertEqual(by["CAMERA"]["flags"], pl.FLAG_UNSUPPORTED_MAPPER)
        self.assertEqual(by["BAD SIZE"]["flags"], pl.FLAG_BAD_SIZE)
        # Offsets: sector aligned, increasing, no gaps larger than the sector padding.
        offset = LAYOUT["PB_ROM_AREA_OFFSET"]
        for e in entries:
            self.assertEqual(e["offset"], offset)
            offset += pl.align_up(e["size"], 4096)
        self.assertEqual(entries[0]["offset"], 0x101000)

    def test_bad_header_checksum_and_ram_code(self):
        bad = syn.make_rom("BADSUM", 0x00, 0, 0, bad_checksum=True)
        self.assertTrue(pl.analyse_rom(bad, "x")["flags"] & pl.FLAG_BAD_HEADER)
        big_ram = syn.make_rom("RAM5", 0x03, 0, 5)  # RAM code 5: the core cannot size it
        self.assertTrue(pl.analyse_rom(big_ram, "x")["flags"] & pl.FLAG_BAD_HEADER)
        bad_size_code = syn.make_rom("SIZE9", 0x00, 9, 0)
        self.assertTrue(pl.analyse_rom(bad_size_code, "x")["flags"] & pl.FLAG_BAD_HEADER)

    def test_name_falls_back_to_the_file_name(self):
        rom = bytearray(syn.make_rom("X", 0, 0, 0))
        rom[0x134:0x144] = b"\0" * 16
        chk = 0
        for i in range(0x134, 0x14D):
            chk = (chk - rom[i] - 1) & 0xFF
        rom[0x14D] = chk
        self.assertEqual(pl.analyse_rom(bytes(rom), "my game")["name"], "my game")

    def test_too_small_is_rejected(self):
        with self.assertRaises(pl.PackError):
            pl.analyse_rom(b"\0" * 100, "tiny")

    def test_uf2_structure(self):
        index, uf2, entries = pl.pack(syn.GOLDEN_ROMS, LAYOUT)
        blocks = parse_uf2(uf2)
        total = len(blocks)
        start = LAYOUT["PB_FLASH_XIP_BASE"] + LAYOUT["PB_LIB_INDEX_OFFSET"]
        payload = b""
        for i, (m0, m1, flags, addr, size, no, tot, fam, end, data) in enumerate(blocks):
            self.assertEqual((m0, m1, end), (pl.UF2_MAGIC_START0, pl.UF2_MAGIC_START1, pl.UF2_MAGIC_END))
            self.assertEqual(flags, pl.UF2_FLAG_FAMILY)
            self.assertEqual(fam, pl.UF2_FAMILY_RP2040)
            self.assertEqual((size, no, tot), (256, i, total))
            self.assertEqual(addr, start + i * 256)  # contiguous, ascending
            payload += data
        # Nothing outside the index sector + ROM area, whole sectors only.
        last = start + len(payload)
        self.assertEqual(len(payload) % 4096, 0)
        self.assertEqual(blocks[0][3], 0x10100000)
        self.assertLessEqual(last, LAYOUT["PB_FLASH_XIP_BASE"] + LAYOUT["PB_ROM_AREA_END"])
        # Content: the index first, then each ROM at its recorded offset.
        self.assertEqual(payload[:4096], index)
        for (name, rom), e in zip(syn.GOLDEN_ROMS, entries):
            rel = e["offset"] - LAYOUT["PB_LIB_INDEX_OFFSET"]
            self.assertEqual(payload[rel:rel + len(rom)], rom)
            pad_end = rel + pl.align_up(len(rom), 4096)
            self.assertEqual(payload[rel + len(rom):pad_end], b"\xFF" * (pad_end - rel - len(rom)))

    def test_too_many_roms(self):
        small = syn.make_rom("S", 0x00, 0, 0)
        with self.assertRaises(pl.PackError) as cm:
            pl.pack([(f"r{i}", small) for i in range(86)], LAYOUT)
        self.assertIn("85", str(cm.exception))
        pl.pack([(f"r{i}", small) for i in range(85)], LAYOUT)  # exactly full is fine

    def test_rom_area_capacity_boundary(self):
        eight_mib = syn.make_rom("HUGE", 0x00, 8, 0)           # 8 MiB
        half_mib = syn.make_rom("HALF", 0x00, 4, 0)            # 512 KiB
        one_mib = syn.make_rom("ONE", 0x00, 5, 0)              # 1 MiB
        pl.pack([("a", eight_mib), ("b", half_mib)], LAYOUT)   # 8.5 MiB fits in 9.0 MiB
        with self.assertRaises(pl.PackError) as cm:
            pl.pack([("a", eight_mib), ("b", one_mib)], LAYOUT)  # 9 MiB does not (area is 0x8FF000)
        self.assertIn("ROM area full", str(cm.exception))

    def test_cli_writes_files(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            d = pathlib.Path(d)
            paths = []
            for name, rom in syn.GOLDEN_ROMS[:3]:
                p = d / f"{name}.gb"
                p.write_bytes(rom)
                paths.append(str(p))
            out = d / "lib.uf2"
            idx = d / "idx.bin"
            rc = pl.main(paths + ["-o", str(out), "--dump-index", str(idx)])
            self.assertEqual(rc, 0)
            self.assertEqual(len(idx.read_bytes()), 4096)
            self.assertEqual(len(out.read_bytes()) % 512, 0)
            self.assertEqual(pl.main(["-o", str(out), str(d / "missing.gb")]), 1)


if __name__ == "__main__":
    unittest.main()
