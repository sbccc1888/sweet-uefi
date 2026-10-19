#!/bin/bash
# build.sh -- build sweet-uefi.efi with nothing but clang and lld-link.
#
# No edk2, no gnu-efi. clang emits an aarch64 COFF object for the
# aarch64-pc-win32-coff target, and lld-link turns it into a PE32+ EFI
# application. That keeps the whole toolchain to two binaries that any distro
# ships, which matters because this project already asks the reader to bring a
# cross toolchain for the kernel.
set -euo pipefail

cd "$(dirname "$0")"

OUT=${OUT:-out}
CLANG=${CLANG:-clang}
LLDLINK=${LLDLINK:-lld-link}
TTF=${TTF:-/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf}
SIZES=${SIZES:-44,28,20}

mkdir -p "$OUT"

# 1. Font atlas. Regenerated only when missing or when the baker changed, so the
#    500 KB generated file does not churn on every build.
if [ ! -f src/font.c ] || [ tools/bake_font.py -nt src/font.c ]; then
	if [ ! -f "$TTF" ]; then
		echo "error: no font at $TTF" >&2
		echo "       set TTF=/path/to/font.ttf (DejaVuSans or Lato both work)" >&2
		exit 1
	fi
	echo "  bake  src/font.c"
	python3 tools/bake_font.py --ttf "$TTF" --out src/font.c --sizes "$SIZES"
fi

# 2. Compile. -mgeneral-regs-only keeps the compiler from emitting FP/SIMD in a
#    context where the firmware may not have enabled it; the renderer is integer
#    maths only, so nothing is lost.
CFLAGS="-target aarch64-pc-win32-coff -std=c11 -O2 -fshort-wchar -ffreestanding \
	-fno-stack-protector -fno-builtin -mgeneral-regs-only -mno-outline-atomics \
	-Wall -Wextra -Wno-unused-parameter -Iinclude -Isrc"

objs=()
for f in src/main.c src/gfx.c src/ui.c src/payload.c src/chain.c src/font.c; do
	o="$OUT/$(basename "${f%.c}").o"
	echo "  cc    $f"
	$CLANG $CFLAGS -c "$f" -o "$o"
	objs+=("$o")
done

# 3. Link as an EFI application.
echo "  link  $OUT/sweet-uefi.efi"
$LLDLINK /subsystem:efi_application /entry:efi_main /machine:arm64 \
	/nodefaultlib /nologo /opt:ref /opt:icf \
	/out:"$OUT/sweet-uefi.efi" "${objs[@]}"

ls -l "$OUT/sweet-uefi.efi"
file "$OUT/sweet-uefi.efi"
