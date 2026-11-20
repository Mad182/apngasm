# APNG Assembler (`2.91-ezgif`)

Creates optimized APNG animations from PNG/TGA image sequences or filmstrips.

This is a maintained fork of [APNG Assembler 2.91](http://apngasm.sourceforge.net/) by Max Stepin (which has not been updated since 2017), maintained for [ezgif.com](https://ezgif.com/).

- **Prebuilt binaries (Linux x86_64 & Windows x64):** [GitHub Releases](https://github.com/Mad182/apngasm/releases)
- **License:** zlib license (altered version of the original software; `7z/` is LGPL, `zopfli/` is Apache 2.0)

---

## Recommended Options

| Goal | Recommended flags | Why |
|---|---|---|
| **Fastest encoding** | `-z3 -zl6` | Uses `libdeflate` level 6 — 1.2–1.6× faster than `zlib -zl6`, often with smaller output too. |
| **Fast + good compression** | `-z3 -zl9` | Faster than `zlib -zl6` on most inputs, with smaller files than `zlib -zl6`. Great default for web servers. |
| **Best speed/size balance** | `-z3 -zl12` | **2.7–2.9× faster than 7zip (`-z1`)** at equal or smaller file sizes, and faster + 2.5–5% smaller than `zlib -zl9`. |
| **Smallest lossless file** | `-z2 -i15` | Zopfli compression — slowest, typically 1–3% smaller than `libdeflate -zl12` / `7zip`. |
| **Smallest lossy file** | `--fuzz 3 -z3 -zl12` | Merges near-identical pixels between frames (3% threshold) so inter-frame transparency compresses video/noisy inputs much smaller. |
| **Bit-identical to 2.91** | `-z0` (or `-z1`) | Exact same compressed image data as upstream 2.91, just 1.2–1.5× faster. |

---

## Benchmarks

Tested on Intel Core i7-7700K, Debian 13, GCC 14.2, zlib 1.3.1, libpng 1.6.48, libdeflate 1.23. Wall time, single run. All lossless outputs were verified pixel-identical (`ffmpeg -f md5`).

### 1. Compression Methods (`2.91-ezgif`)

| Input | Method | Time | Size (bytes) | vs `zlib -zl9` | vs `7zip (-z1)` |
|---|---|---:|---:|---|---|
| **RGB video**<br>180f, 640×360 | `-z0 -zl6` (zlib 6) | 23.45s | 76,156,378 | 1.8× faster, +0.7% | — |
| | `-z0 -zl9` (zlib 9) | 43.34s | 75,637,137 | baseline | — |
| | `-z3 -zl6` (libdeflate 6) | **20.16s** | 76,261,817 | 2.1× faster, +0.8% | — |
| | `-z3 -zl9` (libdeflate 9) | 21.95s | 76,102,941 | 2.0× faster, +0.6% | — |
| | `-z3 -zl12` (libdeflate 12) | 32.21s | **73,769,814** | **1.35× faster, -2.5%** | — |
| **RGBA + alpha**<br>59f, 640×360 | `-z0 -zl6` (zlib 6) | 3.26s | 12,909,722 | 1.2× faster, +0.3% | 5.8× faster, +2.8% |
| | `-z0 -zl9` (zlib 9) | 3.91s | 12,870,625 | baseline | 4.9× faster, +2.5% |
| | `-z3 -zl6` (libdeflate 6) | **2.01s** | 13,174,387 | 1.9× faster, +2.4% | 9.4× faster, +4.9% |
| | `-z3 -zl12` (libdeflate 12) | 6.62s | **12,486,807** | -3.0% size | **2.9× faster, -0.5%** |
| | `-z1` (7zip, 15 iter) | 18.98s | 12,554,853 | -2.5% size | baseline |
| **255-color palette**<br>59f, 640×360 | `-z0 -zl6` (zlib 6) | 2.94s | 4,630,505 | 3.6× faster, +3.9% | 5.7× faster, +9.7% |
| | `-z0 -zl9` (zlib 9) | 10.59s | 4,457,034 | baseline | 1.6× faster, +5.6% |
| | `-z3 -zl6` (libdeflate 6) | **2.33s** | 4,578,857 | 4.5× faster, +2.7% | 7.2× faster, +8.5% |
| | `-z3 -zl9` (libdeflate 9) | 2.99s | 4,515,341 | 3.5× faster, +1.3% | 5.6× faster, +6.9% |
| | `-z3 -zl12` (libdeflate 12) | 6.20s | 4,230,990 | **1.7× faster, -5.1%** | **2.7× faster, +0.2%** |
| | `-z1` (7zip, 15 iter) | 16.88s | **4,222,069** | -5.3% size | baseline |
| **GIF animation**<br>31f, 320×240 | `-z0 -zl9` (zlib 9) | 0.12s | 38,716 | baseline | 7.1× faster, +12.2% |
| | `-z3 -zl6` (libdeflate 6) | **0.08s** | 39,803 | 1.5× faster, +2.8% | 10.6× faster, +15.4% |
| | `-z3 -zl9` (libdeflate 9) | 0.09s | 37,423 | 1.3× faster, -3.3% | 9.4× faster, +8.5% |
| | `-z3 -zl12` (libdeflate 12) | 0.30s | **34,390** | -11.2% size | **2.8× faster, -0.3%** |
| | `-z1` (7zip, 15 iter) | 0.85s | 34,504 | -10.9% size | baseline |

### 2. vs Upstream `2.91` and `apngasm 3.1.10`

Compared against original SourceForge `apngasm 2.91` and [apngasm/apngasm 3.1.10](https://github.com/apngasm/apngasm) (the C++ rewrite, built in Release mode). With `-z0` (default `-zl9`), all three produce **identical compressed image data**; `2.91-ezgif` is 1.2–1.6× faster at identical output and up to 5× faster with `-zl6` or `-z3`.

| Input | `2.91` (`-z0`) | `3.1.10` | `2.91-ezgif` (`-z0`) | `2.91-ezgif` (`-z0 -zl6`) | `2.91-ezgif` (`-z3 -zl12`) |
|---|---:|---:|---:|---:|---:|
| RGB video, 180f 640×360 | 61.1s | 61.8s | 42.8s (**1.4×**) | 22.5s (**2.7×**) | 32.2s (**1.9×**, -2.5% size) |
| RGBA + alpha, 59f 640×360 | 6.1s | 6.3s | 4.0s (**1.6×**) | 3.1s (**2.0×**) | 6.6s (-3.0% size) |
| 255-color palette, 59f 640×360 | 15.2s | 14.3s | 10.4s (**1.4×**) | 2.8s (**5.1×**) | 6.2s (**2.3×**, -5.1% size) |
| GIF animation, 31f 320×240 | 0.13s | 0.13s | 0.11s (**1.2×**) | 0.08s (**1.6×**) | 0.30s (-11.2% size) |

---

## Changes in `2.91-ezgif`

### Performance
- **Skip duplicate frame candidates:** for each frame, `apngasm` evaluates up to 6 dispose/blend candidates. Candidates that would encode the exact same pixels as an already tested one are skipped (eliminates ~50% of work on full-frame video).
- **Early-abort losing candidates:** trial compression stops as soon as its output exceeds the smallest candidate found so far.
- **Hash-table color lookup:** palette reduction uses an open-addressing hash map instead of a 256-entry linear scan per pixel.
- **libdeflate (`-z3`):** whole-buffer high-speed DEFLATE compression (levels 1–12).
- **Default `-O3` build.**

### New Options
- `-o file` — specify output file explicitly (positional output as first argument still works).
- `file ##` / `-d##` — per-frame delays in milliseconds when passing multiple input files.
- `-l N` / `-loop N` — loop count (`-l2` still works).
- `--fuzz N` — lossy inter-frame optimization: pixels differing from the previous frame by up to `N%` (`0–100`) are snapped to the previous value so they compress as unchanged/transparent.
- `-z3` — `libdeflate` compression.
- `-zl#` — compression level for `-z0` (`1–9`, default `9`) or `-z3` (`1–12`, default `12`).

### Bug Fixes & Maintenance
- Reject mismatched frame dimensions instead of crashing with out-of-bounds memory access.
- Scale 16-bit APNG frame delays cleanly instead of silently wrapping around when delays (or merged duplicate frames) exceed 65,535. Fixed 32-bit overflow in delay fraction addition.
- Prevent invalid `PLTE`/`tRNS` chunks from being written into color types that forbid them (e.g., RGBA/gray+alpha after upconversion or with `-kc`, or suggested palettes from RGB inputs).
- Preserve `tRNS` correctly when upconverting grayscale to RGB or gray+alpha.
- Guard against zero/oversized PNG/TGA dimensions (capped at 100000×100000 and 2 GB per frame) and TGA RLE row overflows.
- Fix crash when `-hs`/`-vs` is passed without an input file.
- Fall back to zlib instead of corrupting output if 7zip, Zopfli, or libdeflate fails.
- Fix 256-byte filename buffer overflow and `%` format-string handling in single-file sequence loading.
- Null pointers in `Image::free()`.
- Updated bundled Zopfli to latest upstream (fixes GCC allocation-size warning), pruned 36 unused LZMA/decoder files from `7z/`, and removed outdated 2016 `libpng/`, `zlib/`, and VS/Xcode project files.

---

## Usage

```sh
apngasm output.png frame001.png [options]
apngasm output.png frame*.png   [options]
apngasm -o output.png frame1.png 50 frame2.png 100 [options]
```

### Options

| Option | Description |
|---|---|
| `1 10` | Global frame delay in seconds as numerator/denominator (default `1/10` s) |
| `-o file` | Output file path |
| `file ##` | With multiple input files, delay of that frame in milliseconds |
| `-d##` | Delay of the preceding input file in milliseconds |
| `-l2`, `-l 2`, `-loop 2` | Number of loops (`0` = forever, default) |
| `-f` | Skip the first frame (static fallback image for non-APNG viewers) |
| `-hs##` | Input is a horizontal filmstrip of `##` frames (e.g. `-hs12`) |
| `-vs##` | Input is a vertical filmstrip of `##` frames (e.g. `-vs12`) |
| `-kp` | Keep palette |
| `-kc` | Keep color type |
| `--fuzz N` | Lossy inter-frame optimization, merge pixels within `N%` (`0–100`) |
| `-z0` | zlib compression |
| `-z1` | 7zip compression (default) |
| `-z2` | Zopfli compression |
| `-z3` | libdeflate compression |
| `-zl#` | Compression level for `-z0` (`1–9`, default `9`) or `-z3` (`1–12`, default `12`) |
| `-i##` | Number of iterations for `-z1` / `-z2` (default `-i15`) |

> Note: Options must start with `-` (the Windows `/` prefix from 2.91 is not supported because it clashes with Unix paths).

### Examples

```sh
# Sequence frame01.png, frame02.png, ... at 3/4 s per frame, fast + small with libdeflate
apngasm output.png frame01.png 3 4 -z3 -zl12

# Explicit files with per-frame millisecond delays
apngasm -o output.png a.png 100 b.png 50 c.png 200 -z3 -zl9

# Horizontal filmstrip of 29 frames
apngasm output.png strip.png -hs29 -z3

# Lossy inter-frame optimization for video frames
apngasm -o output.png frame001.png 40 frame002.png 40 --fuzz 3 -z3 -zl12
```

If `frame02.txt` exists alongside `frame02.png` with a line like `delay=25/100`, it overrides that frame's delay in sequence mode.

---

## Building on Debian / Ubuntu

Tested on Debian 12 (bookworm) and Debian 13 (trixie).

1. Install the compiler and development libraries:
   ```sh
   sudo apt update
   sudo apt install build-essential git libpng-dev zlib1g-dev libdeflate-dev
   ```
   (`7z/` and `zopfli/` are bundled in the repository.)

2. Clone and compile:
   ```sh
   git clone https://github.com/Mad182/apngasm.git
   cd apngasm
   make -j"$(nproc)"
   ```
   Compiler warnings from the bundled `7z/` and `zopfli/` sources are harmless.

3. Install (optional, to `/usr/local/bin` by default):
   ```sh
   sudo make install
   ```
   - User-local install (no `sudo`): `make install PREFIX=$HOME/.local`
   - Packaging: `make install DESTDIR=/tmp/pkg`
   - Uninstall: `sudo make uninstall`

4. Verify:
   ```sh
   apngasm
   ```

### Build Options
- Clean build files: `make clean`
- Custom optimization flags: `make CFLAGS_OPT=-O2` (avoid `-march=native` if copying binaries to other CPUs)
- Fully static Linux binary:
  ```sh
  make LIBS="-static -lstdc++ -lpng -lz -ldeflate -lm"
  ```
- Build without `libdeflate`:
  ```sh
  make CFLAGS="-Wall -pedantic -DFEATURE_7ZIP -DFEATURE_ZOPFLI" LIBS="-lstdc++ -lpng -lz -lm"
  ```

---

## Building for Windows

Prebuilt `apngasm.exe` binaries are available on the [Releases](https://github.com/Mad182/apngasm/releases) page.

To cross-compile from Debian/Ubuntu using MinGW-w64:

```sh
sudo apt install mingw-w64 cmake make curl ca-certificates
sh scripts/build-windows.sh
```

This downloads current `zlib`, `libpng`, and `libdeflate` sources (versions and SHA-256 checksums pinned in the script), builds them statically, and produces a standalone `build-windows/apngasm.exe` with no external DLL dependencies.

---

## Original 2.91 Changelog

- **2.91:** Code refactoring, small optimizations, filmstrip (`-hs`/`-vs`) input support.
- **2.9:** Duplicate frame optimization (merges frame delays).
- **2.8:** Added 7zip and Zopfli compression options.
- **2.7:** Better optimization after loading paletted PNG files.
- **2.6:** Fixed loading 1, 2, 4-bit paletted PNG files.
- **2.5:** Fixed loading RGB/grayscale input files with `tRNS`.
- **2.4:** Reads delay information from `.txt` files; accepts mixed color types; zlib license.
