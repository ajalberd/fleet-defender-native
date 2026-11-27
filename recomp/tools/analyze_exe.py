"""
Analyze FALCONCD.EXE / HORNETCD.EXE / MIGCD.EXE:
- MZ header & relocation table
- .RTLink/Plus overlay table structure
- Hardware/DOS interrupt usage & string signatures
"""
import os
import re
import struct
from collections import Counter

GAME_DIR = r"c:\Users\Andrew\Downloads\claude_projects\falcon30_recomp\game_data"


def analyze_exe(filename):
    path = os.path.join(GAME_DIR, filename)
    with open(path, "rb") as f:
        data = f.read()

    last_page, pages, relocs, hdr_paras, min_alloc, max_alloc, ss, sp, csum, ip, cs, reloc_off, ovl = struct.unpack(
        "<13H", data[2:28]
    )
    hdr_size = hdr_paras * 16
    mz_end = (pages - 1) * 512 + (last_page if last_page else 512)
    code = data[hdr_size:mz_end]
    overlay = data[mz_end:]

    entry = cs * 16 + ip
    print(f"================ {filename} ================")
    print(f"File size:    {len(data):,} bytes")
    print(f"MZ header:    {hdr_size:,} bytes ({relocs} relocs @ 0x{reloc_off:x})")
    print(f"MZ image:     {len(code):,} bytes (ends @ file 0x{mz_end:x})")
    print(f"Overlay data: {len(overlay):,} bytes")
    print(f"CS:IP = {cs:04x}:{ip:04x} (image offset 0x{entry:x}), SS:SP = {ss:04x}:{sp:04x}")
    print(f"MinAlloc = {min_alloc} paras ({min_alloc*16:,} B), MaxAlloc = {max_alloc} paras")
    print(f"Entry bytes:  {code[entry:entry+32].hex(' ')}")

    # Parse relocation table to find segment range
    reloc_segs = set()
    for i in range(relocs):
        off, seg = struct.unpack("<HH", data[reloc_off + i * 4 : reloc_off + i * 4 + 4])
        reloc_segs.add(seg)
    print(f"Reloc segments: count={len(reloc_segs)}, min=0x{min(reloc_segs):04x}, max=0x{max(reloc_segs):04x}")

    # Scan for INT nn in resident code
    int_res = Counter()
    for i in range(len(code) - 1):
        if code[i] == 0xCD:
            int_res[code[i + 1]] += 1
    print("Top INTs in resident MZ:", [(f"0x{k:02x}", v) for k, v in int_res.most_common(15)])

    # Check for interesting hardware/DOS strings
    patterns = [
        rb"EMMXXXX0",
        rb"XMS",
        rb"EMS",
        rb"DPMI",
        rb"VCPI",
        rb"8087",
        rb"287",
        rb"387",
        rb"MPU",
        rb"ROLAND",
        rb"MT-?32",
        rb"ADLIB",
        rb"BLASTER",
        rb"THRUSTMASTER",
        rb"JOYSTICK",
        rb"MOUSE",
        rb"VESA",
        rb"VGA",
        rb"CON\x00",
    ]
    print("Hardware/System string hits:")
    for pat in patterns:
        matches = re.findall(rb"[ -~]{0,25}" + pat + rb"[ -~]{0,25}", data, re.IGNORECASE)
        if matches:
            sample = [m.decode("ascii", "ignore").strip() for m in matches[:4]]
            print(f"  {pat.decode('ascii', 'ignore'):14s} ({len(matches):3d}): {sample}")


if __name__ == "__main__":
    analyze_exe("FALCONCD.EXE")
