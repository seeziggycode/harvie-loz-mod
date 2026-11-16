#!/bin/bash
# Package soh.elf for the RG40XX H (KNULLI) as a self-contained port payload:
#   soh.elf.aarch64 + libs.aarch64/ (every library the device does not provide)
# The launcher puts $GAMEDIR/libs.aarch64 on LD_LIBRARY_PATH.
#
# Provided by the device (checked with ldd on it, Nov 2026) and NOT bundled:
#   glibc, libstdc++/libgcc_s, SDL2 2.32 (with the Mali video backend), GLESv2/EGL
#   (libmali), and SDL2's own dependencies.
set -euo pipefail
ELF=$1; OUT=$2
rm -rf "$OUT"; mkdir -p "$OUT/libs.aarch64"
cp "$ELF" "$OUT/soh.elf.aarch64"
ldd "$ELF" | awk '/=> \//{print $1, $3}' | while read -r so path; do
    case "$so" in
        libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|ld-linux*|libresolv.so*|\
        libstdc++.so*|libgcc_s.so*|\
        libGL*|libEGL*|libOpenGL*|libGLX*|libGLdispatch*|libmali*|libdrm*|libgbm*|\
        libSDL2-2.0.so*|libX*|libxcb*|libwayland*|libxkbcommon*|libdecor*|\
        libasound*|libpulse*|libsndfile*|libdbus*|libsystemd*|libudev*|libcap*|\
        libgcrypt*|libgpg-error*|libffi*|libsamplerate*|libpipewire*)
            echo "  device provides  $so"; continue;;
    esac
    cp -L "$path" "$OUT/libs.aarch64/$so"; echo "  bundled          $so"
done
echo "--- unresolved with the bundle on the path (must be empty apart from device libs):"
LD_LIBRARY_PATH="$OUT/libs.aarch64" ldd "$OUT/soh.elf.aarch64" | grep 'not found' || echo "  none"
echo "--- highest symbol versions required (device must have at least these):"
for f in "$OUT/soh.elf.aarch64" "$OUT"/libs.aarch64/*; do objdump -T "$f" 2>/dev/null; done \
    | grep -oE 'GLIBC_[0-9.]+|GLIBCXX_[0-9.]+' | sort -t_ -k2 -V -u | awk -F_ '{v[$1]=$0} END {for (k in v) print "  " v[k]}'
du -sh "$OUT"
