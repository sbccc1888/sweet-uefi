#!/usr/bin/env python3
"""preview.py -- render the menu on the host so the layout can be checked.

Compiling an EFI application proves nothing about how it looks. This builds the
same renderer for the host, feeds it a fake framebuffer, and writes a PNG, so a
layout mistake is visible here instead of on a phone.

The C sources are compiled unchanged; only efi.h's typedefs are satisfied by a
shim header, and gfx_present() writes into the PNG's buffer instead of a panel.

Usage:
  tools/preview.py --out preview.png [--width 1080 --height 2160]
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SHIM = r'''
/* Host shim so the UI sources compile unmodified. */
#ifndef PREVIEW_SHIM_H
#define PREVIEW_SHIM_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int8_t   INT8;
typedef int16_t  INT16;
typedef int32_t  INT32;
typedef int64_t  INT64;
typedef uint8_t  BOOLEAN;
typedef uint16_t CHAR16;
typedef uint64_t UINTN;
typedef int64_t  INTN;
typedef UINTN    EFI_STATUS;
typedef void    *EFI_HANDLE;
typedef void    *EFI_EVENT;
#define EFIAPI
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define EFI_SUCCESS 0
#define EFI_ERROR(s) (((INT64)(s)) < 0)
#define EFI_TRUE 1
#define EFI_FALSE 0

typedef struct { UINT32 Data1; UINT16 Data2; UINT16 Data3; UINT8 Data4[8]; } EFI_GUID;
typedef struct { UINT32 RedMask, GreenMask, BlueMask, ReservedMask; } EFI_PIXEL_BITMASK;
typedef struct {
	UINT32 Version, HorizontalResolution, VerticalResolution, PixelFormat;
	EFI_PIXEL_BITMASK PixelInformation;
	UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;
typedef struct {
	UINT32 MaxMode, Mode;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
	UINTN SizeOfInfo;
	UINT64 FrameBufferBase;
	UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
	void *QueryMode, *SetMode, *Blt;
	EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};
typedef struct { char opaque[1]; } EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct { char opaque[1]; } EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef struct { char opaque[1]; } EFI_TABLE_HEADER;
typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
struct EFI_BOOT_SERVICES {
	EFI_STATUS (*AllocatePool)(UINT32 type, UINTN size, void **out);
	EFI_STATUS (*FreePool)(void *p);
	EFI_STATUS (*LocateProtocol)(EFI_GUID *guid, void *reg, void **iface);
	EFI_STATUS (*Stall)(UINTN microseconds);
};
EFI_STATUS host_allocate_pool(UINT32 type, UINTN size, void **out);
EFI_STATUS host_free_pool(void *p);
typedef struct {
	EFI_TABLE_HEADER Hdr;
	CHAR16 *FirmwareVendor;
	UINT32 FirmwareRevision;
	EFI_HANDLE ConsoleInHandle;
	EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
	EFI_HANDLE ConsoleOutHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
	EFI_HANDLE StandardErrorHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
	void *RuntimeServices;
	EFI_BOOT_SERVICES *BootServices;
} EFI_SYSTEM_TABLE;

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
	{ 0x9042a9de, 0x23dc, 0x4a38, \
	  { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

enum { PixelRedGreenBlueReserved8BitPerColor, PixelBlueGreenRedReserved8BitPerColor,
       PixelBitMask, PixelBltOnly };

#endif
'''

# Renderer for the preview: call ui_frame once, then dump the back buffer.
RENDER = r'''
#include "gfx.h"
#include "ui.h"
#include <stdio.h>

EFI_STATUS host_allocate_pool(UINT32 type, UINTN size, void **out)
{
	(void)type;
	*out = calloc(1, (size_t)size);
	return *out != 0 ? 0 : 1;
}

EFI_STATUS host_free_pool(void *p)
{
	free(p);
	return 0;
}

static EFI_BOOT_SERVICES g_bs;

int main(int argc, char **argv)
{
	struct gfx g;
	struct ui_state ui;
	UINT32 W = 1080, H = 2160;
	int    frame = 0, selected = 0;
	const char *out = "preview.png";

	if (argc > 1) out = argv[1];
	if (argc > 2) W = (UINT32)atoi(argv[2]);
	if (argc > 3) H = (UINT32)atoi(argv[3]);
	if (argc > 4) selected = atoi(argv[4]);
	int items = (argc > 5) ? atoi(argv[5]) : 4;

	g.width = W; g.height = H; g.stride = W;
	g.shift_r = 16; g.shift_g = 8; g.shift_b = 0;
	g.back = calloc((size_t)W * H, 4);
	g.front = g.back;
	if (!g.back) return 1;

	ui.title = "ios-sweet";
	ui.subtitle = "Xiaomi Mi MIX 2 - chiron";
	ui.status = "No SWEETXNU payload on the boot volume.";
	{
		static const char *labels[] = {
			"shim.efi", "sweet-uefi.efi", "bootaa64.efi", "payload.bin",
			"tools.efi", "recovery.efi", "diags.efi", "extra.efi",
		};
		static const char *details[] = {
			"196 KB  EFI image", "42 KB  EFI image", "118 KB  EFI image",
			"payload", "9 KB  EFI image", "77 KB  EFI image",
			"15 KB  EFI image", "3 KB  EFI image",
		};
		ui.item_count = (UINT32)items;
		if (ui.item_count > 8) ui.item_count = 8;
		for (UINT32 i = 0; i < ui.item_count; i++) {
			ui.items[i].label = labels[i];
			ui.items[i].detail = details[i];
			ui.items[i].kind = UI_KIND_BOOT;
		}
	}
	ui.selected = (UINT32)selected;
	ui.highlight_y = 0;
	ui.countdown_total_ms = 5000;
	ui.countdown_ms = 3200;
	ui.anim_ms = 0;

	/* two frames so the animated highlight has settled once */
	ui_frame(&g, &ui, 16);
	(void)frame;
	ui_frame(&g, &ui, 16);

	FILE *f = fopen(out, "wb");
	if (!f) return 1;

	/* Raw dump: "SWP1", then width, height as u32 LE, then RGB8 rows.
	 * Encoding a PNG here would duplicate a solved problem; the caller
	 * converts with Pillow. */
	{
		UINT32 w = W, h = H;
		unsigned char hdr[12] = { 'S','W','P','1', 0,0,0,0, 0,0,0,0 };
		hdr[4] = (unsigned char)(w & 0xFF); hdr[5] = (unsigned char)((w >> 8) & 0xFF);
		hdr[6] = (unsigned char)((w >> 16) & 0xFF); hdr[7] = (unsigned char)((w >> 24) & 0xFF);
		hdr[8] = (unsigned char)(h & 0xFF); hdr[9] = (unsigned char)((h >> 8) & 0xFF);
		hdr[10] = (unsigned char)((h >> 16) & 0xFF); hdr[11] = (unsigned char)((h >> 24) & 0xFF);
		fwrite(hdr, 1, sizeof(hdr), f);
		for (UINT32 y = 0; y < h; y++) {
			for (UINT32 x = 0; x < w; x++) {
				UINT32 c = g.back[(size_t)y * w + x];
				unsigned char px[3];
				px[0] = (unsigned char)((c >> 16) & 0xFF);
				px[1] = (unsigned char)((c >> 8) & 0xFF);
				px[2] = (unsigned char)(c & 0xFF);
				fwrite(px, 1, 3, f);
			}
		}
	}
	fclose(f);
	fprintf(stderr, "wrote %s (%ux%u)\n", out, W, H);
	return 0;
}
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="preview.png")
    ap.add_argument("--width", type=int, default=1080)
    ap.add_argument("--height", type=int, default=2160)
    ap.add_argument("--selected", type=int, default=1)
    ap.add_argument("--items", type=int, default=4)
    a = ap.parse_args()

    tmp = tempfile.mkdtemp(prefix="sweet-ui-preview-")
    shim = os.path.join(tmp, "shim")
    os.makedirs(shim, exist_ok=True)
    with open(os.path.join(shim, "efi.h"), "w") as f:
        f.write(SHIM)
    with open(os.path.join(shim, "preview_main.c"), "w") as f:
        f.write(RENDER)
    # use the real font.h and copy it in
    with open(os.path.join(ROOT, "src", "font.h")) as f:
        fh = f.read()

    srcs = [os.path.join(ROOT, "src", s) for s in ("gfx.c", "ui.c", "font.c")]
    srcs.append(os.path.join(shim, "preview_main.c"))
    exe = os.path.join(tmp, "preview")
    cmd = ["cc", "-O1", "-std=gnu11", "-I" + shim, "-I" + os.path.join(ROOT, "src"),
           "-o", exe] + srcs + ["-lm"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr[-4000:], file=sys.stderr)
        raise SystemExit("host build failed")

    out = os.path.abspath(a.out)
    raw = os.path.join(tmp, "frame.raw")
    r = subprocess.run([exe, raw, str(a.width), str(a.height), str(a.selected),
                       str(a.items)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr[-2000:], file=sys.stderr)
        raise SystemExit("render failed")

    from PIL import Image
    with open(raw, "rb") as f:
        blob = f.read()
    if blob[:4] != b"SWP1":
        raise SystemExit("bad raw dump magic")
    w = int.from_bytes(blob[4:8], "little")
    h = int.from_bytes(blob[8:12], "little")
    px = blob[12:]
    if len(px) != w * h * 3:
        raise SystemExit("raw size mismatch: %d vs %d" % (len(px), w * h * 3))
    img = Image.frombytes("RGB", (w, h), px)
    img.save(out)
    print("preview: %s  %dx%d  %d bytes" % (out, w, h, os.path.getsize(out)))


if __name__ == "__main__":
    main()
