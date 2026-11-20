#!/bin/sh
# Cross-compile a static Windows x64 apngasm.exe on Debian/Ubuntu (MinGW-w64).
#
#   sudo apt install mingw-w64 cmake make curl ca-certificates
#   sh scripts/build-windows.sh
#
# Downloads and builds current zlib and libpng (the copies in zlib/ and
# libpng/ are from 2016 and are not used). Result: build-windows/apngasm.exe,
# a single self-contained executable (no DLLs needed).
set -eu

ZLIB_VER=1.3.2
ZLIB_SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
PNG_VER=1.6.59
PNG_SHA256=2540302a1844ad2b2b501977abecfa850f265f97b78f065a712ab4074a89f5b5

HOST=${HOST:-x86_64-w64-mingw32}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$ROOT/build-windows
DEPS=$WORK/deps
JOBS=$(nproc 2>/dev/null || echo 2)

mkdir -p "$WORK" "$DEPS/include" "$DEPS/lib"
cd "$WORK"

fetch() { # url file sha256
  [ -f "$2" ] || curl -fsSL -o "$2" "$1"
  echo "$3  $2" | sha256sum -c -
}

fetch "https://github.com/madler/zlib/releases/download/v$ZLIB_VER/zlib-$ZLIB_VER.tar.gz" \
      "zlib-$ZLIB_VER.tar.gz" "$ZLIB_SHA256"
fetch "https://github.com/pnggroup/libpng/archive/refs/tags/v$PNG_VER.tar.gz" \
      "libpng-$PNG_VER.tar.gz" "$PNG_SHA256"

# zlib
rm -rf "zlib-$ZLIB_VER" && tar xzf "zlib-$ZLIB_VER.tar.gz"
make -C "zlib-$ZLIB_VER" -f win32/Makefile.gcc PREFIX="$HOST-" CFLAGS="-O2 -Wall" -j"$JOBS" libz.a
cp "zlib-$ZLIB_VER/zlib.h" "zlib-$ZLIB_VER/zconf.h" "$DEPS/include/"
cp "zlib-$ZLIB_VER/libz.a" "$DEPS/lib/"

# libpng
rm -rf "libpng-$PNG_VER" png-build && tar xzf "libpng-$PNG_VER.tar.gz"
cmake -S "libpng-$PNG_VER" -B png-build \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER="$HOST-gcc" \
  -DCMAKE_RC_COMPILER="$HOST-windres" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$DEPS" \
  -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF \
  -DZLIB_INCLUDE_DIR="$DEPS/include" -DZLIB_LIBRARY="$DEPS/lib/libz.a"
cmake --build png-build -j"$JOBS"
cmake --install png-build

# apngasm (built in a copy so it doesn't mix with a native build's obj/)
rm -rf src && mkdir src
cp -r "$ROOT"/*.cpp "$ROOT"/*.h "$ROOT"/Makefile "$ROOT"/7z "$ROOT"/zopfli src/
make -C src -j"$JOBS" \
  PACKAGE=apngasm.exe CC="$HOST-gcc" \
  CFLAGS="-Wall -DFEATURE_7ZIP -DFEATURE_ZOPFLI -I$DEPS/include" \
  LIBS="-static -L$DEPS/lib -lstdc++ -lpng16 -lz -lm"
cp src/apngasm.exe "$WORK/apngasm.exe"
"$HOST-strip" "$WORK/apngasm.exe"
echo "Built $WORK/apngasm.exe"

