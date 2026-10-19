/*
 * font.h -- access to the baked antialiased glyph atlases.
 */
#ifndef SWEET_UEFI_FONT_H
#define SWEET_UEFI_FONT_H

#include "efi.h"

enum {
	FONT_TITLE = 0,   /* large heading    */
	FONT_BODY  = 1,   /* menu entries     */
	FONT_SMALL = 2,   /* hints and status */
	FONT_COUNT
};

struct baked_glyph {
	UINT16 codepoint;
	INT16  offset_x;   /* ink offset from the pen, x */
	INT16  offset_y;   /* ink offset from the baseline, y */
	UINT16 w, h;       /* ink size                   */
	UINT16 advance;    /* pen advance                */
	UINT16 atlas_x, atlas_y;
};

struct baked_font {
	UINT32 size;       /* pixel size the atlas was rasterised at */
	UINT32 ascent, descent, line_height;
	UINT32 atlas_w, atlas_h;
	const unsigned char *atlas;          /* 8-bit alpha, one byte per pixel */
	const struct baked_glyph *glyphs;
	UINT32 glyph_count;
};

extern const struct baked_font g_fonts[FONT_COUNT];

#endif /* SWEET_UEFI_FONT_H */
