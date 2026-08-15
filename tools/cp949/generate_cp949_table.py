#!/usr/bin/env python3
"""Emit src/core/text/Cp949Table.cpp from Python's own cp949 codec.

OJN headers store their title, artist, charter and sample-package name as
CP949 (Unified Hangul Code, a superset of EUC-KR) bytes. The renderer has to
decode them itself: it also runs as WebAssembly, where there is no iconv, ICU
or Win32 to lean on. So the mapping is baked into the binary as one dense
table indexed by (lead - 0x81, trail - 0x41), which is the full rectangle a
CP949 double-byte sequence can occupy. Cells that map to nothing hold 0.

Usage:
    python tools/cp949/generate_cp949_table.py > src/core/text/Cp949Table.cpp
"""

import hashlib
import sys

LEAD_FIRST, LEAD_LAST = 0x81, 0xFE
TRAIL_FIRST, TRAIL_LAST = 0x41, 0xFE
LEAD_COUNT = LEAD_LAST - LEAD_FIRST + 1
TRAIL_COUNT = TRAIL_LAST - TRAIL_FIRST + 1


def build_table() -> list[int]:
    table: list[int] = []
    for lead in range(LEAD_FIRST, LEAD_LAST + 1):
        for trail in range(TRAIL_FIRST, TRAIL_LAST + 1):
            try:
                text = bytes([lead, trail]).decode("cp949")
            except UnicodeDecodeError:
                table.append(0)
                continue
            assert len(text) == 1, (lead, trail, text)
            code_point = ord(text)
            # Everything CP949 encodes lives in the BMP, so uint16_t suffices.
            assert 0 < code_point <= 0xFFFF and not (0xD800 <= code_point <= 0xDFFF), (lead, trail, code_point)
            table.append(code_point)
    return table


def main() -> None:
    table = build_table()
    assert len(table) == LEAD_COUNT * TRAIL_COUNT
    mapped = sum(1 for value in table if value)
    raw = b"".join(value.to_bytes(2, "little") for value in table)
    digest = hashlib.sha256(raw).hexdigest()

    out = sys.stdout
    out.write("// GENERATED FILE - do not edit by hand.\n")
    out.write("// Produced by tools/cp949/generate_cp949_table.py from Python's cp949 codec\n")
    out.write(f"// (Python {sys.version.split()[0]}). {mapped} of {len(table)} cells are mapped;\n")
    out.write(f"// SHA-256 of the little-endian uint16 table: {digest}\n")
    out.write("//\n")
    out.write("// Layout: kCp949Table[(lead - 0x81) * 190 + (trail - 0x41)] is the Unicode\n")
    out.write("// code point of the CP949 sequence {lead, trail}, or 0 when unmapped.\n\n")
    out.write('#include "core/text/Cp949.hpp"\n\n')
    out.write("namespace renderojn::text::detail {\n\n")
    out.write("const std::uint16_t kCp949Table[kCp949LeadCount * kCp949TrailCount] = {\n")
    for row in range(0, len(table), 16):
        cells = ", ".join(f"0x{value:04X}" for value in table[row:row + 16])
        out.write(f"    {cells},\n")
    out.write("};\n\n")
    out.write("} // namespace renderojn::text::detail\n")


if __name__ == "__main__":
    main()
