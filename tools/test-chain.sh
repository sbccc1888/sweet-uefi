#!/bin/bash
# test-chain.sh -- host test of the chainloader's device path build.
#
# Device path construction is pointer arithmetic over a variable-length structure.
# A mistake there does not fail to compile; it hands LoadImage a malformed path and
# the firmware either rejects it or walks off the end of the buffer.
set -uo pipefail
cd "$(dirname "$0")/.."

OUT=${OUT:-/tmp/sweet-chain-test}
mkdir -p "$OUT"

echo "chainloader device path test"
cc -O1 -std=gnu11 -Wall -Wextra -Wno-unused-function -Iinclude -Isrc -fshort-wchar \
	-o "$OUT/chain-test" tests/chain_devpath_test.c || {
	echo "build failed" >&2
	exit 1
}
"$OUT/chain-test"
rc=$?

if [ $rc -eq 0 ]; then
	echo "chain device path test OK"
else
	echo "chain device path test FAILED" >&2
fi
exit $rc
