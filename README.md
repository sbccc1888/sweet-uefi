# sweet-uefi

A graphical UEFI boot menu for the [ios-sweet](https://github.com/dipa-studio/ios-sweet)
XNU port on ARM64 phones, targeting the Xiaomi Mi MIX 2 (`chiron`, MSM8998) first.

![The menu](docs/preview.png)

## What it is

A single freestanding EFI application. It takes the framebuffer over GOP, draws a
menu with an antialiased software renderer, and is meant to be the thing that
decides what boots. Drop it on the ESP as `\EFI\BOOT\BOOTAA64.EFI` and the phone's
normal boot picks it up -- no custom fastboot image involved.

No edk2, no gnu-efi. `clang` emits an `aarch64-pc-win32-coff` object and
`lld-link` produces a PE32+ EFI application, so the whole toolchain is two
binaries any distro ships.

## Build

```sh
./build.sh                     # -> out/sweet-uefi.efi
TTF=/path/to/font.ttf ./build.sh
CLANG=clang-18 LLDLINK=lld-link-18 ./build.sh
```

Needs `clang`, `lld-link`, and `python3-pil` (Pillow rasterises the font atlas).
The generated `src/font.c` is ~490 KB and is rebuilt only when the baker or the
typeface changes.

## Verify it

```sh
./test.sh
```

Four gates: the EFI image builds and really is PE32+ AArch64 **(1)**; the menu
renders at both 4 and 8 entries and its text fits inside the highlight pill
**(2)**; `include/efi.h` matches the UEFI specification's field offsets **(3)**;
and the chainloader's device paths tile correctly **(4)**.

Gate 3 exists because `efi.h` is transcribed by hand from the spec instead of
vendored from edk2. One missing Boot Services entry shifts every pointer after
it, and the failure at runtime is arbitrary rather than a clean error. The check
compiles a probe, reads `offsetof()` for each field this application calls, and
compares against the values chapter 4.4 implies -- `EFI_BOOT_SERVICES` must be
exactly 368 bytes (24-byte header + 43 function pointers). The first version of
that check had three wrong expectations (`ConOut`, the total size, and the
ordering of `Stall` vs `ExitBootServices`); the header turned out to be correct
and the test was fixed.

## The device path test

Gate 4 exists because `chain.c` builds a device path by pointer arithmetic over a
variable-length on-disk structure. Get it wrong and `LoadImage` receives a
malformed path: the firmware rejects it, or walks past the end of the buffer.
Nothing about that fails to compile.

`tests/chain_devpath_test.c` compiles `chain.c` unchanged -- against the real
`efi.h`, so the structures under test are the shipped ones -- with only the boot
services it calls stubbed out. It then walks the produced path and checks the
invariants UEFI 2.10 chapter 10.3 requires: every node's length includes its own
4-byte header, the nodes exactly tile the buffer, the volume prefix survives byte
for byte, the FILEPATH node carries a NUL-terminated UTF-16 name, and there is
exactly one END node.

I checked that it can actually fail by injecting three bugs one at a time; each
was caught with a specific message:

| injected bug | what the test said |
|---|---|
| volume END node not dropped | `no FILEPATH node`, `expected 3 nodes, got 2` |
| FILEPATH length excludes the NUL | `FILEPATH node is 14 bytes, expected 16` |
| node length written big-endian | `FILEPATH node is 4096 bytes, expected 16` |

72 checks. A test that cannot fail is not a test, and this one was verified to
fail in three distinct ways.

## Look at it without a phone

```sh
tools/preview.py --out preview.png --width 1080 --height 2160
tools/check_layout.py preview.png
```

`preview.py` compiles the **same** `gfx.c`/`ui.c`/`font.c` for the host against a
small shim and renders one frame to a PNG. `check_layout.py` then measures the
accent pill and the text inside it and fails if the text does not fit.

Both exist because compiling an EFI binary proves nothing about whether it looks
right. The first render from this tool showed every space in the UI missing --
the words came out as `Bootoptions` and `XiaomiMiMIX2-chiron` -- and a second
pass caught the selected row's second line hanging outside its highlight. Neither
would have been visible any other way short of flashing a phone.

## What it boots

Two different things, deliberately kept apart.

**EFI images are chained for real.** Every `.efi` on the volume becomes a menu
entry, and selecting one runs it through the standard UEFI mechanism:

```
LoadImage(TRUE, self, file_device_path, NULL, 0, &handle);
StartImage(handle, NULL, NULL);
```

The device path is assembled the way edk2's `FileDevicePath()` does it -- the
volume's own path with its END node replaced by a MEDIA_FILEPATH node -- so
`LoadImage` resolves the file through the Simple File System protocol itself.
When the launched image returns, control comes back here, which is what lets this
be a menu rather than a one-way jump.

Search order, and the order entries appear:

```
\sweet\*.efi      the documented place for a payload
\*.efi            anything else at the root
```

**The XNU kernel is not an EFI image**, and this launcher does not pretend
otherwise. It is a Mach-O payload that `shim/loader.c` copies into place and
enters with the MMU off. Doing that here would duplicate reviewed code and, worse,
would put the segment copy and the MMU transition in this application. So the
`SWEETXNU payload` row reports what it found and says where the handoff actually
happens. Once the shim is built as (or wrapped in) an EFI application it appears
in this menu and boots through the path above with no change here.

Payload search order for that row:

```
\sweet\payload.bin
\sweet\xnu-payload.bin
\payload.bin
```

With no `.efi` files present the menu holds instead of counting down, because
counting down into a row that cannot run would be worse than waiting.

## Layout

```
include/efi.h        the UEFI subset used, transcribed from the spec
src/gfx.{c,h}        software renderer: fill, gradient, rounded rect, AA text
src/ui.{c,h}         the menu: layout, animation, countdown
src/payload.{c,h}    SWEETXNU payload discovery and validation
src/chain.{c,h}      LoadImage/StartImage chainloading of .efi files
src/main.c           GOP setup, input loop, frame pacing
src/font.{c,h}       generated antialiased glyph atlases
tools/bake_font.py   TTF -> alpha atlases (+ per-glyph rects)
tools/preview.py     host render for layout review
tools/check_layout.py  asserts the text fits its pill
tools/check_efi_layout.py  asserts efi.h matches the specification's offsets
tools/test-chain.sh  device path arithmetic
tests/chain_devpath_test.c  its 72 checks
test.sh              all four gates
```

## Notes on the UEFI declarations

`include/efi.h` is written from the UEFI 2.10 specification rather than copied
from edk2, so it stays small and the structures match the spec field for field.
Boot Services in particular is transcribed in full order, because one missing
entry silently shifts every pointer after it. `EFI_FILE_PROTOCOL.GetInfo` takes
four arguments (`This, InformationType, BufferSize, Buffer`); getting that wrong
is a compile error here, but the same class of mistake in a hand-written struct
would be a crash on hardware.

## Status

- Builds to a 193 KB PE32+ AArch64 EFI application; `./test.sh` passes.
- The renderer, menu, input loop and payload discovery are exercised by the host
  preview; the EFI binary itself has **not** been booted on a phone yet.
- The `Boot options` / `Hardware` / `Reboot` panels are placeholders and say so
  when selected.
- Chainloading is implemented but has not been exercised against a real image on
  real firmware; the device-path arithmetic is the part most worth watching.
- The XNU payload handoff stays in the shim; see above.

## Licence

MIT.
