#!/usr/bin/env python3
"""Locate combat_speed string xrefs and nearby calls in Mewgenics.exe...

This script is intended to help keep the combat-speed hook points up to date
across game builds...
"""

from __future__ import annotations

from pathlib import Path
import struct

EXE = Path("Game data/Mewgenics.exe")
TEXT_FILE_OFF = 0x400
TEXT_VA = 0x140001000
TEXT_SIZE = 0x00E300A4
RDATA_FILE_OFF = 0x00E30600
RDATA_VA = 0x140E32000


def file_off_to_va(file_off: int) -> int:
    if TEXT_FILE_OFF <= file_off < TEXT_FILE_OFF + TEXT_SIZE:
        return TEXT_VA + (file_off - TEXT_FILE_OFF)
    if RDATA_FILE_OFF <= file_off:
        return RDATA_VA + (file_off - RDATA_FILE_OFF)
    raise ValueError("offset not in mapped sections")


def va_to_text_off(va: int) -> int:
    rel = va - TEXT_VA
    if rel < 0 or rel >= TEXT_SIZE:
        raise ValueError("va not in .text")
    return rel


def scan_rip_relative_xrefs(text: bytes, target_va: int) -> list[int]:
    """Find common 7-byte RIP-relative forms that reference target_va..."""
    out: list[int] = []
    for i in range(0, len(text) - 7):
        rex = text[i]
        opcode = text[i + 1]
        modrm = text[i + 2]
        if 0x40 <= rex <= 0x4F and opcode in (0x8D, 0x8B, 0x89, 0xC7) and (modrm & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", text, i + 3)[0]
            insn_va = TEXT_VA + i
            computed = insn_va + 7 + disp
            if computed == target_va:
                out.append(insn_va)
    return out


def find_nearby_calls(text: bytes, around_va: int, radius: int = 0x80) -> list[int]:
    center_off = va_to_text_off(around_va)
    start = max(0, center_off - radius)
    end = min(len(text) - 5, center_off + radius)
    out: list[int] = []
    for i in range(start, end):
        if text[i] != 0xE8:
            continue
        disp = struct.unpack_from("<i", text, i + 1)[0]
        call_va = TEXT_VA + i
        target = call_va + 5 + disp
        out.append(target)
    return sorted(set(out))


def main() -> int:
    blob = EXE.read_bytes()
    text = blob[TEXT_FILE_OFF : TEXT_FILE_OFF + TEXT_SIZE]

    off = blob.find(b"combat_speed\x00")
    if off < 0:
        raise SystemExit("combat_speed not found!!!!")

    combat_speed_va = file_off_to_va(off)
    print(f"combat_speed file offset: 0x{off:X}")
    print(f"combat_speed VA:          0x{combat_speed_va:X}")

    xrefs = scan_rip_relative_xrefs(text, combat_speed_va)
    print(f"RIP-relative code xrefs:  {len(xrefs)}")
    for x in xrefs:
        print(f"  - 0x{x:X} (RVA 0x{x - 0x140000000:X})")
        calls = find_nearby_calls(text, x)
        for c in calls[:8]:
            print(f"      call -> 0x{c:X} (RVA 0x{c - 0x140000000:X})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
