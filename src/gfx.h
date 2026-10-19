/*
 * gfx.h -- a small software renderer for the UEFI framebuffer.
 *
 * Double buffered on purpose: drawing straight into the scanout buffer makes
 * every animation tear, and the whole point of this application is that it
 * looks like it was made this decade. All pixels are 32-bit; the channel order
 * is resolved once at setup from the mode's PixelFormat/PixelBitmask, so the
 * same drawing code works on RGB and BGR panels.
 */
#ifndef SWEET_UEFI_GFX_H
#define SWEET_UEFI_GFX_H

#include "efi.h"
#include "font.h"

/* 0xAARRGGBB, the format every call below takes */
typedef UINT32 colour_t;

#define RGB(r, g, b)      ((colour_t)(0xFF000000u | ((r) << 16) | ((g) << 8) | (b)))
#define RGBA(r, g, b, a)  ((colour_t)(((a) << 24) | ((r) << 16) | ((g) << 8) | (b)))
#define ALPHA(c)          ((UINT8)(((c) >> 24) & 0xFF))

/* Linear blend of two ARGB colours; t is 0..256 (256 = fully `to`). */
static inline colour_t
gfx_mix(colour_t from, colour_t to, UINT32 t)
{
	UINT32 inv = 256u - t;
	UINT32 r = (((from >> 16) & 0xFF) * inv + ((to >> 16) & 0xFF) * t) >> 8;
	UINT32 g = (((from >> 8) & 0xFF) * inv + ((to >> 8) & 0xFF) * t) >> 8;
	UINT32 b = ((from & 0xFF) * inv + (to & 0xFF) * t) >> 8;
	UINT32 a = (((from >> 24) & 0xFF) * inv + ((to >> 24) & 0xFF) * t) >> 8;

	return (a << 24) | (r << 16) | (g << 8) | b;
}

struct gfx {
	UINT32  width, height;
	UINT32  stride;         /* in pixels */
	UINT32 *back;           /* drawing target */
	UINT32 *front;          /* the panel (GOP framebuffer) */
	UINT8   shift_r, shift_g, shift_b;   /* to panel order */
};

/* Bind to the current GOP mode and allocate the back buffer. 0 on success. */
int  gfx_init(EFI_SYSTEM_TABLE *st, struct gfx *g);

/* Copy the back buffer to the panel. */
void gfx_present(struct gfx *g);

/* Clear the back buffer. */
void gfx_clear(struct gfx *g, colour_t c);

void gfx_pixel(struct gfx *g, INT32 x, INT32 y, colour_t c);
void gfx_fill(struct gfx *g, INT32 x, INT32 y, UINT32 w, UINT32 h, colour_t c);

/* Vertical gradient from top to bottom. */
void gfx_vgradient(struct gfx *g, INT32 x, INT32 y, UINT32 w, UINT32 h,
    colour_t top, colour_t bottom);

/* Rounded rectangle; radius is clamped to half the smaller side. */
void gfx_round_rect(struct gfx *g, INT32 x, INT32 y, UINT32 w, UINT32 h,
    UINT32 radius, colour_t c);

/* Rounded rectangle outline, `thickness` pixels wide. */
void gfx_round_outline(struct gfx *g, INT32 x, INT32 y, UINT32 w, UINT32 h,
    UINT32 radius, UINT32 thickness, colour_t c);

/*
 * Antialiased text. `y` is the TOP OF THE LINE BOX, not a baseline: the baked
 * glyph rects carry their ink offset within the box, so a line occupies
 * [y, y + f->line_height). Returns the pen x after the last glyph.
 */
INT32 gfx_text(struct gfx *g, INT32 x, INT32 y, const char *s,
    const struct baked_font *f, colour_t c);

/* Width the string will occupy at that size. */
UINT32 gfx_text_width(const char *s, const struct baked_font *f);

/* Text centred horizontally in [x, x+w); `y` is the line box top. */
void gfx_text_centred(struct gfx *g, INT32 x, INT32 y, UINT32 w,
    const char *s, const struct baked_font *f, colour_t c);

#endif /* SWEET_UEFI_GFX_H */
