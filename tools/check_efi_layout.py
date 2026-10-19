#!/usr/bin/env python3
"""check_efi_layout.py -- verify include/efi.h against the UEFI specification.

efi.h is transcribed by hand from the spec rather than vendored from edk2, which
keeps it small but means a field could be missing or out of order. That failure
is invisible at build time and lethal at run time: if one Boot Services entry is
missing, every pointer after it is shifted, and the first call that is not at
offset 0 does something arbitrary.

This compiles a tiny probe against the header and checks the reported offsets
against the values the specification implies for a 64-bit platform:

    EFI_TABLE_HEADER     24 bytes  (u64 + 4 * u32)
    SystemTable.ConOut   at 0x40   (Hdr 24, Vendor 8, Revision 4, pad 4,
                                    ConInHandle 8, ConIn 8)
    SystemTable.BootServices at 0x60
    BootServices.Header  first, then RaiseTPL at 0x18
    BootServices.GetNextMonotonicCount and Stall must exist and be ordered

The checks are structural rather than exhaustive: they pin the fields this
application actually calls, plus the total size, which catches an omission
anywhere in the middle.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

PROBE = r'''
#include <stdio.h>
#include <stddef.h>
#include "efi.h"

int main(void)
{
	printf("sizeof_EFI_TABLE_HEADER %zu\n", sizeof(EFI_TABLE_HEADER));
	printf("off_ConOut %zu\n", offsetof(EFI_SYSTEM_TABLE, ConOut));
	printf("off_BootServices %zu\n", offsetof(EFI_SYSTEM_TABLE, BootServices));
	printf("size_EFI_SYSTEM_TABLE %zu\n", sizeof(EFI_SYSTEM_TABLE));
	printf("off_RaiseTPL %zu\n", offsetof(EFI_BOOT_SERVICES, RaiseTPL));
	printf("off_AllocatePool %zu\n", offsetof(EFI_BOOT_SERVICES, AllocatePool));
	printf("off_HandleProtocol %zu\n",
	    offsetof(EFI_BOOT_SERVICES, HandleProtocol));
	printf("off_LocateProtocol %zu\n",
	    offsetof(EFI_BOOT_SERVICES, LocateProtocol));
	printf("off_Stall %zu\n", offsetof(EFI_BOOT_SERVICES, Stall));
	printf("off_ExitBootServices %zu\n",
	    offsetof(EFI_BOOT_SERVICES, ExitBootServices));
	printf("off_GetNextMonotonicCount %zu\n",
	    offsetof(EFI_BOOT_SERVICES, GetNextMonotonicCount));
	printf("off_CopyMem %zu\n", offsetof(EFI_BOOT_SERVICES, CopyMem));
	printf("size_EFI_BOOT_SERVICES %zu\n", sizeof(EFI_BOOT_SERVICES));
	printf("size_EFI_FILE_PROTOCOL %zu\n", sizeof(EFI_FILE_PROTOCOL));
	printf("size_EFI_GRAPHICS_OUTPUT_PROTOCOL %zu\n",
	    sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL));
	return 0;
}
'''

# Expected values for a 64-bit platform, derived from UEFI 2.10 chapter 4.
EXPECT = {
    # 8 signature + 4 + 4 + 4 + 4
    "sizeof_EFI_TABLE_HEADER": 24,
    # Hdr(24) FirmwareVendor(24) FirmwareRevision(32) pad(36..40)
    # ConsoleInHandle(40) ConIn(48) ConsoleOutHandle(56) ConOut(64)
    "off_ConOut": 64,
    # ... StdErr(80) RuntimeServices(88) BootServices(96)
    "off_BootServices": 96,
    # Header(24) then the first function pointer
    "off_RaiseTPL": 24,
    # 24 + 5 pointers: Raise, Restore, AllocatePages, FreePages, GetMemoryMap
    "off_AllocatePool": 64,
    # 24 + 16 pointers
    "off_HandleProtocol": 152,
    # 24 + 27 pointers
    "off_LocateProtocol": 320,
    # 24 + 26 pointers
    "off_Stall": 248,
    # 24 + 26 pointers, one before GetNextMonotonicCount
    "off_ExitBootServices": 232,
    "off_GetNextMonotonicCount": 240,
    # 24 + 41 pointers
    "off_CopyMem": 352,
    # 24 + 43 pointers
    "size_EFI_BOOT_SERVICES": 368,
}


def main():
    tmp = tempfile.mkdtemp(prefix="sweet-efi-layout-")
    src = os.path.join(tmp, "probe.c")
    exe = os.path.join(tmp, "probe")
    with open(src, "w") as f:
        f.write(PROBE)

    cmd = ["cc", "-O0", "-I" + os.path.join(ROOT, "include"), "-o", exe, src]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print("  probe build failed:\n" + r.stderr[-2000:])
        return 1

    r = subprocess.run([exe], capture_output=True, text=True)
    if r.returncode != 0:
        print("  probe run failed")
        return 1

    got = {}
    for line in r.stdout.splitlines():
        k, v = line.split()
        got[k] = int(v)

    fail = 0
    for key, want in EXPECT.items():
        if key not in got:
            print("  MISSING %s" % key)
            fail += 1
            continue
        mark = "ok  " if got[key] == want else "FAIL"
        if got[key] != want:
            fail += 1
        print("  %s %-32s %d (expected %d)" % (mark, key, got[key], want))

    # Ordering checks: each of these must be strictly after the previous one.
    # Spec order (4.4): ... ExitBootServices, GetNextMonotonicCount, Stall ...
    order = ["off_RaiseTPL", "off_AllocatePool", "off_HandleProtocol",
             "off_ExitBootServices", "off_GetNextMonotonicCount",
             "off_Stall", "off_LocateProtocol", "off_CopyMem"]
    prev = -1
    for key in order:
        if key not in got:
            print("  MISSING %s" % key)
            fail += 1
            continue
        if got[key] <= prev:
            print("  FAIL %s is not after the previous entry (%d <= %d)"
                  % (key, got[key], prev))
            fail += 1
        prev = got[key]

    # Exact size: 24-byte header plus 43 function pointers. A missing entry
    # shows up here as a multiple of 8, which is why the exact value is pinned
    # rather than bounded.
    size = got.get("size_EFI_BOOT_SERVICES", 0)
    if size != EXPECT["size_EFI_BOOT_SERVICES"]:
        print("  FAIL BootServices is %d bytes, spec says %d (%d pointer(s) off)"
              % (size, EXPECT["size_EFI_BOOT_SERVICES"],
                 (size - EXPECT["size_EFI_BOOT_SERVICES"]) // 8))
        fail += 1

    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
