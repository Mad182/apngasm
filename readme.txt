  APNG Assembler 2.91-ezgif

  Creates APNG animation from PNG/TGA image sequence.

  Fork of APNG Assembler 2.91 by Max Stepin, which has not been updated
  since 2017. Maintained by Madars for ezgif.com, where it is used to build
  and optimize animated PNGs.

  Fork:     https://github.com/Mad182/apngasm
  Original: http://apngasm.sourceforge.net/

  Prebuilt binaries (Linux x86_64, Windows x64):
            https://github.com/Mad182/apngasm/releases

  Copyright (c) 2010-2016 Max Stepin
  maxst@users.sourceforge.net

  Modifications (c) 2026 Madars (ezgif.com)

  License: zlib license (this is an altered version of the original
  software, see the license text in apngasm.cpp)

--------------------------------

  Changes in version 2.91-ezgif:

  Output is byte-for-byte identical to 2.91 for the same input and
  options (apart from the version string in the tEXt chunk), just faster.

Performance:
- Frame encoding: for every frame apngasm test-compresses up to 6
  candidates (3 dispose ops x 2 blend ops) and keeps the smallest.
  - Candidates that would encode exactly the same pixels as an already
    tested one are skipped (common for full-frame changes such as video,
    which removes about half of the work).
  - Trial compression of a candidate stops as soon as its output exceeds
    the best size found so far, since it can no longer win.
- Color type reduction (RGBA/RGB/gray+alpha to indexed): colors are looked
  up in a hash table instead of a linear search over up to 256 palette
  entries for every pixel of every frame.
- Default build uses -O3.

New options:
- -o file      : output file (the output can also be given first, as before)
- file ##      : with multiple input files, the number after a file is that
                 frame's delay in milliseconds:
                 apngasm -o out.png a.png 100 b.png 50 c.png 200
- -d##         : delay of the preceding input file in milliseconds
- -l N, -loop N: number of loops (-l2 still works)
- --fuzz N     : lossy inter-frame optimization: pixels that differ from the
                 previous frame by up to N% (0-100) are replaced with the
                 previous value, so they become transparent/unchanged in the
                 frame difference and compress much better
- -zl#         : zlib level for the final -z0 pass, 1-9. The default -zl9 is
                 the same as 2.91; -zl6 is 1.3-3.7x faster with output only
                 0.3-4% bigger

Bug fixes:
- Input frames of different sizes (multiple files) caused out-of-bounds
  reads/writes and crashes; now rejected with an error message.
- Frame delays are 16-bit in APNG. Long delays, or delays of merged
  duplicate frames added together, silently wrapped around (two identical
  40 s frames became 14.5 s). They are now scaled to fit. The delay merging
  arithmetic also overflowed 32 bits with mixed denominators.
- PLTE/tRNS chunks could be written into images where the PNG spec forbids
  them (e.g. a leftover tRNS in gray+alpha/RGBA output after color type
  conversion, or with -kc, or a "suggested palette" from an RGB input file),
  producing invalid PNG files.
- Gray -> RGB and gray -> gray+alpha conversion lost/misplaced the tRNS
  transparent color.
- Crash on PNG/TGA files with zero or huge dimensions (missing allocation
  check); image size is now limited to 100000x100000 and 2 GB per frame.
- TGA loader: RLE packets could overflow the row buffer on malformed files.
- Crash when -hs/-vs was given without an input file.
- If 7zip or Zopfli compression failed, the previous frame's compressed size
  was reused, corrupting the output; now falls back to zlib.
- Buffer overflow with input file names longer than 255 characters, and a
  '%' in the input file name was interpreted as a printf format.
- Dangling pointers after Image::free().

--------------------------------

  Benchmark: 2.91 vs 2.91-ezgif

  Intel Core i7-7700K, Debian 13, gcc 14.2, zlib 1.3.1, libpng 1.6.48.
  Both built with the same Makefile (-O2 for 2.91, -O3 for 2.91-ezgif),
  same input frames, same options, original command line syntax
  (apngasm out.png frame001.png -z0). Wall time, single run.

  input                                   options   2.91    ezgif  speedup
  --------------------------------------  -------  ------  ------  -------
  video, 180 frames 640x360 RGB             -z0    61.6 s  42.8 s   1.4x
  video + alpha mask, 59 frames 640x360     -z0     6.2 s   4.1 s   1.5x
  video in 255 colors, 59 frames 640x360    -z0    15.1 s  10.7 s   1.4x
  GIF animation, 31 frames 320x240          -z0    0.15 s  0.11 s   1.3x
  video in 255 colors, 59 frames 640x360    -z1    20.0 s  16.0 s   1.2x
  GIF animation, 31 frames 320x240          -z1    0.75 s  0.76 s   1.0x

  Output files are identical in all cases (+6 bytes for the version text).
  With -z1 (7zip) and the default -zl9, most of the time is spent in the
  final compression pass, which is unchanged.

  With the new -zl6 option (zlib level 6 for the final pass), compared to
  2.91 -z0:

  input                                    2.91 -z0   ezgif -z0 -zl6   size
  --------------------------------------   --------   --------------  ------
  video, 180 frames 640x360 RGB             61.6 s     23.1 s (2.7x)   +0.7%
  video + alpha mask, 59 frames 640x360      6.2 s      3.2 s (1.9x)   +0.3%
  video in 255 colors, 59 frames 640x360    15.1 s      2.9 s (5.3x)   +3.9%

  Compared to apngasm 3.1.10 (https://github.com/apngasm/apngasm, the C++
  rewrite, last commit 2021), built with CMake in Release mode on the same
  machine. 3.x is a port of the same 2.9x algorithm without the 7zip/Zopfli
  options, and produces exactly the same image data as 2.91 (only the delay
  encoding and the version text differ). Same frames, 100 ms delay, zlib:

  input                                  3.1.10  2.91-ezgif     ezgif -zl6
  -------------------------------------  ------  -------------  -------------
  video, 180 frames 640x360 RGB          61.8 s  42.8 s (1.4x)  22.5 s (2.7x)
  video + alpha mask, 59 frames 640x360   6.3 s   4.0 s (1.6x)   3.1 s (2.0x)
  video in 255 colors, 59 frames 640x360 14.3 s  10.4 s (1.4x)   2.8 s (5.1x)
  GIF animation, 31 frames 320x240       0.13 s  0.11 s (1.2x)  0.08 s (1.6x)

  File sizes: 2.91-ezgif (default -zl9) output has the same image data as
  3.1.10; -zl6 output is 0.3-4.7% bigger.

--------------------------------

Usage:

apngasm output.png frame001.png [options]
apngasm output.png frame*.png   [options]
apngasm -o output.png frame1.png 50 frame2.png 50 [options]

Options :
1 10    : frame delay is 1/10 sec. (default)
-o file : output file
file ## : (multiple input files) delay of that frame in ms
-d##    : delay of the preceding input file in ms
-l2     : 2 loops (default is 0, forever)
-f      : skip the first frame
-hs##   : input is horizontal strip of ## frames (example: -hs12)
-vs##   : input is vertical strip of ## frames   (example: -vs12)
-kp     : keep palette
-kc     : keep color type
--fuzz N: lossy inter-frame optimization, merge pixels that differ by up to N%
-z0     : zlib compression
-zl#    : zlib level for -z0, 1-9 (default -zl9; -zl6 is much faster)
-z1     : 7zip compression (default)
-z2     : Zopfli compression
-i##    : number of iterations (default -i15)

Options must start with '-' (the '/' prefix of the Windows version is not
supported, since it clashes with absolute paths).

--------------------------------

Building on Debian / Ubuntu

Tested on Debian 12 (bookworm) and Debian 13 (trixie). The same steps work
on Ubuntu and other Debian based distributions.

1. Install the compiler and the libpng/zlib development packages:

   sudo apt update
   sudo apt install build-essential git libpng-dev zlib1g-dev

   (build-essential provides gcc, g++ and make; the 7zip and Zopfli
   compressors are included in the source tree, nothing else is needed)

2. Get the source:

   git clone https://github.com/Mad182/apngasm.git
   cd apngasm

3. Compile:

   make -j"$(nproc)"

   This creates the "apngasm" binary in the current directory. Compiler
   warnings coming from the bundled 7z/ and zopfli/ code are harmless.

4. Install (optional), to /usr/local/bin by default:

   sudo make install

   Other locations: "make install PREFIX=$HOME/.local" (no sudo needed)
   or "make install DESTDIR=/tmp/pkg" for packaging.
   Remove with "sudo make uninstall" (use the same PREFIX).

5. Check that it works:

   apngasm

   prints "APNG Assembler 2.91-ezgif" followed by the usage text.

Building for Windows

Prebuilt apngasm.exe: https://github.com/Mad182/apngasm/releases

To build it yourself, cross-compile on Debian/Ubuntu with MinGW-w64:

   sudo apt install mingw-w64 cmake make curl ca-certificates
   sh scripts/build-windows.sh

The script downloads current zlib and libpng sources (versions and SHA-256
checksums pinned in the script), builds them as static libraries and
produces build-windows/apngasm.exe, a single executable without DLL
dependencies. The old Visual Studio projects in projects/ are from the
original 2.91 and have not been updated.

Build options:
- "make clean" removes the binary and object files.
- Optimization flags are set by CFLAGS_OPT in the Makefile (default
  -O3 -fomit-frame-pointer), e.g. "make CFLAGS_OPT=-O2".
  "-march=native" may make it slightly faster, but the binary will
  then only run on CPUs with the same instruction set as the build
  machine, so don't use it for binaries you copy to other servers.
- The libpng/ and zlib/ directories (2016 versions) are only used by the
  original Visual Studio project files; the Linux build links the system
  libpng and zlib, and scripts/build-windows.sh downloads current ones.
- Fully static Linux binary (as in the releases):
  make LIBS="-static -lstdc++ -lpng -lz -lm"

================================================================

  Original APNG Assembler 2.91 readme follows.

--------------------------------


  Changes in version 2.91:

- Code refactoring, some small optimizations
- CLI: Support for image strips as input
- GUI for Windows: progress bar bug fixed
- 64 bit biraries for Windows

  Changes in version 2.9:

- Duplicate frames optimization (add frame delays)

  Changes in version 2.8:

- Added 7zip and Zopfli compression options
- Added GUI version for Windows

  Changes in version 2.7:

- Better optimization after loading paletted PNG files

  Changes in version 2.6:

- Fixed a bug with loading 1,2,4-bit paletted PNG files

  Changes in version 2.5:

- Fixed a bug with loading mode2 or mode0 input files with tRNS

  Changes in version 2.4:

- Reads delay information from .txt files
- Accepts input files of different color types
- Better optimization
- zlib license

--------------------------------

Usage:

apngasm output.png frame001.png [options]
apngasm output.png frame*.png   [options]

Options :
1 10    : frame delay is 1/10 sec. (default)
-l2     : 2 loops (default is 0, forever)
-f      : skip the first frame
-hs##   : input is horizontal strip of ## frames (example: -hs12)
-vs##   : input is vertical strip of ## frames   (example: -vs12)
-kp     : keep palette
-kc     : keep color type
-z0     : zlib compression
-z1     : 7zip compression (default)
-z2     : Zopfli compression
-i##    : number of iterations (default -i15)

----------------------------------------------------------------
Example 1:

Let's say you have following frame sequence:
frame01.png
frame02.png
frame03.png

And you want to have 3/4 seconds delay between frames.
The correct command will be

apngasm output.png frame01.png 3 4


If frame02.txt is found with the following one-line content,
it will override delay information for frame 2:

delay=25/100

----------------------------------------------------------------
Example 2:

The same as above, but you added "invisible" frame00.png :
frame00.png - invisible
frame01.png
frame02.png
frame03.png

The correct command will be

apngasm output.png frame00.png 3 4 /f

That way APNG supported browsers and image viewers
will show frame01-frame02-frame03 animation,
while IE will display static frame00.png image.

----------------------------------------------------------------
Example 3:

apngasm output.png frame01.png

That way you'll get 1/10 sec delay.

----------------------------------------------------------------
Example 4:

Using this 2900x100 "filmstrip" image as input:
https://abs.twimg.com/a/1470716385/img/animations/web_heart_animation.png

apngasm output.png web_heart_animation.png -hs29

Switch -hs29 specifies that input is horizontal strip of 29 frames

----------------------------------------------------------------
Some optimizations used in APNG Assembler might re-sort the
palette, or change the color type from RGBA and RGB modes
to RGB and indexed modes. Those optimizations are only performed
when they are lossless, but if you want to avoid changing the
palette or colortype, use those switches to turn them off:

/kp     : keep palette
/kc     : keep color type

