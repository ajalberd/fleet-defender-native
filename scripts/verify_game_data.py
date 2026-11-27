#!/usr/bin/env python3
"""
Fleet Defender Gold Native Host — Game Data & SHA-256 Verification Tool

Verifies that a user-supplied Fleet Defender Gold installation in `game_data/`
matches the exact executable SHA-256 hashes supported by this project,
and checks for required native 3D theater (.WLD), model (.3DZ), sound, and
optional Roland CM-32L / MT-32 ROM files.
"""

import argparse
import hashlib
import pathlib
import sys

EXPECTED_EXES = {
    "F14.COM": {
        "title": "Fleet Defender Launcher",
        "size": 3114,
        "sha256": "7fce98eb6021c39580fbb06c7393fb312c3feb4360152eeb032df43882fbc400",
        "required": True,
    },
    "F.EXE": {
        "title": "Fleet Defender Frontend / Career Wrapper",
        "size": 88187,
        "sha256": "d59ca5cf6a3970bff9067837c7d0c521c5ca0bf36f6f8aea86ff11303ab0f02c",
        "required": True,
    },
    "F14.EXE": {
        "title": "Fleet Defender 3D Flight Simulator (F-14 Tomcat)",
        "size": 399166,
        "sha256": "7959ddd91b2f8ac76a5ac1faf20899a43fe4adfe8249afe946ee5ebd60168e76",
        "alt_sha256": {
            "f28f78b54467b81a5f0ed631410d50f2ac6c276a2230c832aac7f1c119814b76": "PixelWings Visual Upgrade Mod 1.0 (Ocean Texture Fix)",
        },
        "required": True,
    },
    "MGRAPHIC.EXE": {
        "title": "MicroProse Graphics Driver Overlay",
        "size": 7192,
        "sha256": "3050c737246b9cef3dd69732b007fe735cdcec7ab08416c1d7adb5994e835816",
        "required": True,
    },
    "F14S.EXE": {
        "title": "Fleet Defender Scenario Builder",
        "size": 160942,
        "sha256": "a9cbe18c3e67367bc8f3585176948b0232e6fde13bfbf10af8c542d00156fa6b",
        "required": False,
    },
    "R.EXE": {
        "title": "Fleet Defender Roster / Auxiliary Tool",
        "size": 66977,
        "sha256": "02750fd18415eba7ad77c83f951468d66fca9cfa3f870fd3a0b32340f429b7fd",
        "required": False,
    },
}

REQUIRED_ASSETS = [
    "NCN01.WLD",
    "MEDN01.WLD",
    "USAN01.WLD",
    "KOR04.WLD",
    "INDIA09.WLD",
    "F14PD01.DAT",
    "F14MD01.DAT",
    "F14RD01.DAT",
    "DIGITAL.F14",
    "GAME.DGT",
    "RSOUND.14G",
    "RSOUND.14W",
    "CONFIG.SND",
    "FONTS.F14",
]


def sha256_file(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def find_ci(root: pathlib.Path, rel: str) -> pathlib.Path | None:
    cur = root
    for part in pathlib.PurePosixPath(rel).parts:
        if not cur.is_dir():
            return None
        match = None
        for entry in cur.iterdir():
            if entry.name.lower() == part.lower():
                match = entry
                break
        if match is None:
            return None
        cur = match
    return cur


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify Fleet Defender Gold game_data/ files & SHA-256 hashes.")
    ap.add_argument("game_dir", nargs="?", default="game_data", help="Path to your Fleet Defender Gold directory (default: game_data)")
    ap.add_argument("--rom-dir", default=None, help="Optional path to Roland CM-32L / MT-32 ROM folder")
    args = ap.parse_args()

    root = pathlib.Path(args.game_dir)
    if not root.is_dir():
        print(f"[ERROR] Directory not found: {root.resolve()}")
        print("        Copy your legally owned Fleet Defender Gold files into 'game_data/' and re-run.")
        return 1

    print(f"Checking Fleet Defender Gold game data in: {root.resolve()}\n")
    ok_all = True

    print("=== Executables (SHA-256) ===")
    for name, info in EXPECTED_EXES.items():
        p = find_ci(root, name)
        if p is None:
            tag = "[FAIL]" if info["required"] else "[INFO]"
            print(f"  {tag} {name:14s} — not found ({info['title']})")
            if info["required"]:
                ok_all = False
            continue
        actual_sha = sha256_file(p)
        actual_sz = p.stat().st_size
        alt_label = info.get("alt_sha256", {}).get(actual_sha)
        if actual_sha == info["sha256"]:
            print(f"  [ OK ] {name:14s} — EXACT MATCH ({info['title']}, {actual_sz:,} B)")
        elif alt_label:
            print(f"  [ OK ] {name:14s} — VERIFIED MOD ({alt_label}, {actual_sz:,} B)")
        else:
            print(f"  [WARN] {name:14s} — SHA-256 MISMATCH ({info['title']})")
            print(f"         Expected: {info['sha256']} ({info['size']:,} B)")
            print(f"         Actual:   {actual_sha} ({actual_sz:,} B)")
            if info["required"]:
                ok_all = False

    print("\n=== Core Assets & 5 Theaters ===")
    for rel in REQUIRED_ASSETS:
        p = find_ci(root, rel)
        if p is not None:
            print(f"  [ OK ] {rel}")
        else:
            print(f"  [FAIL] {rel} — missing")
            ok_all = False

    if args.rom_dir:
        rdir = pathlib.Path(args.rom_dir)
        print(f"\n=== Roland CM-32L / MT-32 ROMs ({rdir}) ===")
        ctrl = find_ci(rdir, "CM32L_CONTROL.ROM") or find_ci(rdir, "MT32_CONTROL.ROM")
        pcm = find_ci(rdir, "CM32L_PCM.ROM") or find_ci(rdir, "MT32_PCM.ROM")
        if ctrl and pcm:
            print(f"  [ OK ] Found {ctrl.name} + {pcm.name}")
        else:
            print("  [WARN] Missing CONTROL/PCM ROMs — Roland MT-32/CM-32L music requires user-supplied ROMs.")

    print()
    if ok_all:
        print("SUCCESS: Fleet Defender Gold game data verified! You are ready to build and run f14_native.")
        return 0
    print("FAILED: One or more required files are missing or have mismatched SHA-256 hashes.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
