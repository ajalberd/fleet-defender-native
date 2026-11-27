# Fleet Defender Gold Native Host — Technical Handoff & Reverse-Engineering Reference

This document records the architecture, verified binary/RAM map of **Fleet Defender Gold** (`F14.COM`, `F.EXE`, `F14.EXE`, `F14S.EXE`), solved low-level pitfalls, and next milestones for `f14_native`.

---

## 1. Architecture & Folder Parity with `falcon30_recomp`

- **Root Layout:** `game_data/` (gitignored GOG/DOS game files), `recomp/` (original C++20 SDL3 + OpenGL + Dear ImGui + Munt `libmt32emu` + `libx86emu` host), `docs/screenshots/`, `scripts/verify_game_data.py`, `launch_f14.bat.example`, `.gitignore`, `LICENSE`, `README.md`.
- **Execution Model:** Hybrid Native Host ("Ship of Theseus") running the real 16-bit Real-Mode MS-DOS binaries directly in conventional RAM + 16 MB EMS 4.0 while replacing 3D rendering, cockpit/padlock, audio output, multi-HOTAS input, and optional 240 Hz F-14 flight physics natively in C++20.

---

## 2. Verified Executables & Process Chain

| Binary | Size | Role | SHA-256 (prefix) |
|---|---:|---|---|
| `F14.COM` | 3,114 B | Resident `.COM` launcher (`PLAYFLC.EXE` → `F.EXE` → `F14.EXE` / `F14S.EXE`) | `7fce98eb6021...` |
| `F.EXE` | 88,187 B | Career / Scramble / Campaign / Briefing / Arming Frontend (packed MZ) | `d59ca5cf6a39...` |
| `F14.EXE` | 399,166 B | 3D F-14 Tomcat Flight Simulator (MS C 7.0 Medium Model, 5,298 relocs) | `7959ddd91b2f...` |
| `MGRAPHIC.EXE` | 7,192 B | MicroProse MCGA/VGA 320×200×256 graphics overlay (`INT 21h AX=4B03h`) | `3050c737246b...` |
| `F14S.EXE` | 160,942 B | Mission / Scenario Builder | `a9cbe18c3e67...` |
| `DIGITAL.F14` | 4,368 B | Sound Blaster digital speech overlay (streams `GAME.DGT` from EMS) | `e26f6296d486...` |
| `RSOUND.14G` / `.14W` | 15,741 / 29,258 B | Roland MT-32 / LAPC-1 game & wrapper music/SFX drivers (`0x330`) | `010d8207e8bb...` |

---

## 3. Critical Low-Level Pitfalls Already Solved (Do Not Regress!)

1. **DOS Process EXEC Stack (`INT 21h AX=4B00h`, `AH=4Dh`, `AH=4Ch`):**
   - `F14.COM` stays resident at `0100:0100` and spawns `PLAYFLC.EXE`, `F.EXE`, `F14.EXE`, and `F14S.EXE` as child processes.
   - Each `McbBlock`, `OpenFile`, and `EmsHandle` is tagged with `owner_psp`; child termination (`AH=4Ch`) frees child MCBs, closes child files, releases leaked EMS handles, resets any IVT vector pointing into the dead child segment back to HLE stubs, and restores the parent `ProcessFrame`.
2. **`F.EXE` Walks DOS MCB Chain via `INT 21h AH=52h` ("Get List of Lists"):**
   - `F.EXE` calls `INT 21h AH=52h` and walks `ES:[BX-2]` until it finds MCB `'Z'`. `DosMachine::int_21h()` populates `SYSVARS` at `0060:0002` with `first_mcb_seg` at `0060:0000` and writes real 16-byte `'M'`/`'Z'` MCB headers at `(seg - 1) << 4`.
3. **Mouse Detection Checks `*(uint8_t*)IVT[33h] != 0xCF` (`IRET`):**
   - Putting `0x90 0xCF` (`NOP; IRET`) at `IVT[33h]` prevents `F.EXE`'s `"Mouse Not Present !!"` dialog and enables full mouse control across `F.EXE` and `F14.EXE`.
4. **Instant Zero-Drift HOTAS via MicroProse ColoSpec BIOS Protocol (`INT 15h AH=84h`):**
   - `JOYSTX.ASM` probes `AX=8400h DX=00F0h` for `AX='SG' (0x5347), BX!=0`. Answering this routes `ReadJoy2` & `Trigger2` straight through `INT 15h AX=8401h DX=1` (returning instant 16-bit axes calibrated to `JOY.F14`'s `15..840`, center `427`, throttle `836..17`), completely avoiding negative-G red-out (`GAMNT=-100`) and slow `0x201` spin loops.
5. **16 MB EMS 4.0 (`EMS_TOTAL_PAGES = 1024`):**
   - `F14.EXE` allocates ~8+ MB of EMS for `.WLD` banks, `.3DZ` models, `.PCX` textures, `GAME.DGT` speech, explosions, and moving RIO heads. Giving 1,024 pages (16 MB) guarantees zero degraded features.
6. **Inherited `libx86emu` Fixes (`compat/x86emu_prim_ops.c` & `deliver_hw_irq()`):**
   - Preserves patched `OUTSB` (`DS:SI` instead of `ES:SI`) and manual IRQ stack frame delivery (never `x86emu_intr_raise()`).
7. **Sound Blaster DMA Channel 3 (`DRQ=3`) Support for Stock GOG Speech:**
   - Stock GOG Fleet Defender Gold defaults digital speech (`DIGITAL.F14` + `GAME.DGT`/`WRAP.DGT`) to **DMA DRQ 3** rather than DRQ 1. `DosMachine` emulates both 8237 DMA Channel 1 (`0x02`/`0x03`/`0x83`) and Channel 3 (`0x06`/`0x07`/`0x82`) and tracks `last_dma_ch_` so speech works out of the box regardless of whether `INSTALL.EXE` configured DRQ 1 or DRQ 3.
8. **MicroProse `INT 33h` Mouse Quirks (`F.EXE` Relative Mickies & `F14S`/`F14` `MouseShift = CH`):**
   - `F.EXE` moves its Main Menu cursor exclusively via `INT 33h AX=000Bh` (Read Motion Counters) and queries `AX=0024h` (driver info). It also divides `(dx, dy)` by `8` until it observes any odd delta (`bit 0 != 0`), after which it latches 1:1 pixel mode. `DosMachine` accumulates `(dx, dy)` mickeys and splits `±1` off the first non-zero even delta so `F.EXE` latches 1:1 immediately with zero drift.
   - `F14S.EXE` and `F14.EXE` (`MOUSE.ASM`) call `INT 33h AX=0000h` immediately followed by `AX=0003h` and set `MouseShift = CH` (`initial_CX >> 8`). `AX=0000h` / `AX=0021h` MUST reset the cursor to center `(160, 100)` (`CX = 320 = 0x0140`, `CH = 1`) so `CX >> 1` spans the full `0..319` width instead of compressing to `0..159` when `CX >= 512` (`CH = 2`).
9. **Strict MCB Paragraph Conservation in `dos_alloc()` / `dos_resize()` (`F14.COM` Leak Check `0xAB8`):**
   - `F14.COM` records the exact segment and byte size of the largest free conventional memory block at startup (`0x0CB4`/`0x0CB6`) and verifies after every child exit (`0xAB8` & `0xB34`) that not even **1 paragraph (16 B)** was lost; if the free block differs by even 1 paragraph, `F14.COM` immediately aborts via `INT 21h AX=4C00h` instead of spawning `F14.EXE`.
   - When `rem == 1` in `dos_alloc()` or `dos_resize()` (cannot split into a 1-para MCB header + `>=1`-para free block), `mcbs_[i].paras` MUST NOT be shrunk by 1, preserving exact paragraph conservation across unlimited child EXEC cycles.
10. **EMS 4.0 `INT 67h AH=57h` (Move/Exchange Memory Region) for `F.EXE` UI Popups:**
    - `F.EXE`'s Scramble/Career popup dialogs (`Our Alt` `qs-altim.PIT`, `Enemy Alt`, `Time` `qs-TIM2.PIT`, etc.) call `INT 67h AH=57h` (`shade.c` line 178) to transfer popup backing stores between conventional RAM/VGA and EMS; without `AH=57h`, `F.EXE` prints `ems transfer failed: error 84` and aborts with exit code 3. `int_67h()` implements full `AH=51h`, `AH=53h`, and `AH=57h` (`AL=00h` Move / `AL=01h` Exchange).

---

## 4. Verified `F14.EXE` Gold DGROUP RAM Map

`F14.EXE` DGROUP is signature-anchored via `ReqdGees[24]` (`16,16,16,16,16,16,17,17,17,17,17,17,17,17,18,18,18,18,18,18,18,19,19,20`) located at `DGROUP + 0x239E` (`load_seg + 0x55F1`), making all offsets 100% load-address independent:

| DGROUP Offset | Type | Symbol / Meaning |
|---|---|---|
| `+0x1C76` | `int16_t` | `BackSeat` (`0` = Pilot, `1` = RIO) |
| `+0x241E` | `int16_t` | `Weight` |
| `+0x2420` | `int16_t[9]` | `FM[3][3]` aircraft rotation matrix |
| `+0x247A` | `uint16_t` | `OurHead` (16-bit BAM: `val * 360 / 65536`) |
| `+0x247C` | `int16_t` | `OurPitch` (16-bit signed BAM) |
| `+0x247E` | `int16_t` | `OurRoll` (16-bit signed BAM) |
| `+0x2480` | `uint16_t` | `Alt` (4-ft units) |
| `+0x2482` | `uint16_t` | `OurAlt` (MSL altitude in feet) |
| `+0x2486` | `int32_t` | `RadarAlt` (AGL altitude in feet) |
| `+0x2498` | `int16_t` | `THRUST` (`0..100` = MIL, `100..144` = Afterburner) |
| `+0x249A` | `int16_t` | `GAMNT` (`-100..+100` Redout / Blackout intensity) |
| `+0x5012` | `uint16_t[2]` | `PlaneDataList` far pointer (`off, seg`; stride `40 B`: `+0=PlaneEnumId, +2=name[8], +38=bclass`) |
| `+0x5530` | `int16_t` | `PadHead`, `+0x5532` = `PadPitch`, `+0x5534` = `PadLockee` (aircraft), `+0x5536` = `PadLockee` (missile) |
| `+0x5FE4` | `uint16_t` | `carrier` far segment (`seg:0x0558`: `x/100, y, z/100, heading, pitch, roll`) |
| `+0x7E5E` | `int32_t` | `ViewX` (20-bit East) |
| `+0x7E64` | `Planes[70]` | `ps[70]` live aircraft & ships (stride **`0x28 = 40 B`**: `+6=alt4, +8=xL, +12=yL, +16=head, +18=pitch, +20=roll, +22=type, +24=status, +26=speed`) |
| `+0x895A` | `int32_t` | `ViewY` (20-bit inverted: unified South = `0x100000 - ViewY`) |
| `+0x8972` | `int16_t` | `ViewZ` (4-ft units) |
| `+0x898C` | `int16_t` | `vRoll` (16-bit signed BAM) |
| `+0x899C` | `Sams[28]` | `ss[28]` live missiles & SAMs (stride **`0x2C = 44 B`**: `+0=x, +4=y, +16=alt4, +20=head, +22=pitch, +24=roll, +26=dist`) |
| `+0x9086` | `int32_t` | `PxLng` (player 20-bit East) |
| `+0x9568` | `int16_t` | `Knots` (KIAS) |
| `+0xAE26` | `int32_t` | `PyLng` (player 20-bit inverted: unified South = `0x100000 - PyLng`) |
| `+0xAE94` | `uint16_t` | `View` (`0=FRONT, 1=LEFT, 2=RIGHT, 3=DOWN, 4=REARLEFT, 5=REARRIGHT, 0x46=FRONTAL, 0x47=PADLOCK, 0x48=PILOT, 0xC9+=External`) |
| `+0xAE9E` | `int16_t` | `vPitch` (16-bit signed BAM) |
| `+0xC65C` | `uint16_t` | `vHead` (16-bit BAM) |

---

## 5. Verified Native 3D Asset Formats

- **`.WLD` (862,992 B across all 5 theaters):** `768 B` palette + `13 × 65,536 B` banks (Bank 0 `HMAP` 256×256 heightmap, Bank 1 `BMAP` biome/tile map, Banks 2..12 stamps/tiles) + `9,216 B` `OBJECTS[96][16]` + `400 B` `GroundObjectScale[200]` + `640 B` `GroundObjectNames[32][20]`.
- **`.3DZ` (236 files):** `u16 radius`, `char tex_pcx[14]`, `u16 matnum, ptsnum, srfnum`, `(matnum-1)*8 B` `matdef`, `srfnum*12 B` `srfdef`, `u16 root + srfnum*2 B` BSP nodes, `ptsnum*8 B` `ptdef`, `srfnum*2 B` GOP byte offsets (`GPPLY=3`, `GPGRDPLY=4`, `GPTXTRPLY=5` w/ 8-bit `(u,v)` into 256×200 `.PCX`).
- **`F14PD01.DAT` (12,012 B = 231 × 52 B):** Maps `PlaneEnumId` → `name[8]` (`+2`), `.3DZ` filename (`+16`), `bclass` (`+50`: `10=Fighter, 20=Bomber, 40=Heli, 60=Carrier, 70=Ship`).
- **`WorldColor[256]` (from `FLIR.C`):** Separates cockpit/HUD (`0` & `2`, with `0x06 = HUD_COLOR`) from 3D world (`1`) with 100% pixel accuracy.

---

## 6. Next Milestones

1. **Multi-View Cockpit Sprite Harvesting & RIO 3D Cockpit:** Extend `harvest_cockpit_views()` to silently capture Left (`KP4`), Right (`KP6`), Rear-Left (`KP1`), Rear-Right (`KP3`), and RIO Back-Seat (`'`) views for full 360° pixel-art Pilot + RIO virtual cockpits.
2. **Articulated `.3DZ` Sub-Matrices & Carrier Island/Deck Detail:** Animate F-14 variable-sweep wings, tailerons, gear, and carrier jet blast deflectors directly from `matdef` matrices and `OBJECTS[96][16]` ground structures.
3. **64-Voice Pre-Baked Roland MT-32 Sample Proxy for Fleet Defender:** Map `RSOUND.14G` MT-32 SFX patches into the 64-voice zero-dropout mixer alongside `GAME.DGT` SB16 speech with automatic engine ducking.
4. **OpenXR VR Support:** Feed OpenXR `XrView` matrices into `ModernRenderer::render_3d_view()` with 6-DOF head tracking inside the F-14 Pilot & RIO cockpits.
