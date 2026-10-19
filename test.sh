#!/bin/bash
# test.sh -- build everything and verify the things that can be verified here.
#
# Three gates, in increasing cost:
#   1. the EFI application builds and is really a PE32+ AArch64 EFI image
#   2. the menu renders and its text fits its highlight pill
#   3. the UEFI structures match the specification's field order (efi.h is
#      transcribed by hand, and one misplaced pointer silently shifts every
#      entry after it)
set -uo pipefail
cd "$(dirname "$0")"

fail=0

echo "== 1. build =="
if ./build.sh > /tmp/sweet-uefi-build.log 2>&1; then
	echo "  built: $(stat -c%s out/sweet-uefi.efi) bytes"
else
	echo "  BUILD FAILED"; tail -20 /tmp/sweet-uefi-build.log; fail=1
fi
if file out/sweet-uefi.efi | grep -q 'PE32+ executable (EFI application) Aarch64'; then
	echo "  it is a PE32+ AArch64 EFI application"
else
	echo "  WRONG FORMAT: $(file -b out/sweet-uefi.efi)"; fail=1
fi

echo "== 2. layout =="
if python3 tools/preview.py --out /tmp/preview.png --width 1080 --height 2160 2>/dev/null; then
	if python3 tools/check_layout.py /tmp/preview.png; then
		echo "  layout ok"
	else
		echo "  LAYOUT FAILED"; fail=1
	fi
else
	echo "  RENDER FAILED (is python3-pil installed?)"; fail=1
fi

echo "== 3. structure layout =="
if python3 tools/check_efi_layout.py; then
	echo "  efi.h field offsets match the specification"
else
	echo "  STRUCTURE MISMATCH"; fail=1
fi

echo
if [ $fail -eq 0 ]; then
	echo "ALL CHECKS PASSED"
else
	echo "FAILURES PRESENT"
fi
exit $fail
