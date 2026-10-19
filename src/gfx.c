/*
 * gfx.c -- software rendering for the UEFI framebuffer. See gfx.h.
 *
 * Everything here is plain integer maths on a 32-bit back buffer. There is no
 * hardware acceleration to speak of: GOP's Blt() is only used by the firmware's
 * own console, and the video path we care about (a phone panel) has none. For a
 * 1080x2160 panel a full-screen fill is ~2.3 M writes, which on this class of
 * SoC is a few milliseconds -- fast enough that the animation timer, not the
 * rasteriser, sets the frame rate.
 */

#include "gfx.h"

static EFI_SYSTEM_TABLE  *g_st;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *g_gop;

static EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

/* Round a division by 255 without a divide instruction per pixel channel. */
static inline UINT32
div255(UINT32 v)
{
	return (v + 128 + ((v + 128) >> 8)) >> 8;
}

int
gfx_init(EFI_SYSTEM_TABLE *st, struct gfx *g)
{
	EFI_STATUS status;
	void *iface = 0;

	g_st = st;
	if (EFI_ERROR(status = g_st->BootServices->LocateProtocol(&gop_guid, 0,
	    &iface))) {
		return -1;
	}
	g_gop = (EFI_GRAPHICS_OUTPUT_PROTOCOL *)iface;
	if (g_gop->Mode == 0 || g_gop->Mode->Info == 0) {
		return -1;
	}

	g->width  = g_gop->Mode->Info->HorizontalResolution;
	g->height = g_gop->Mode->Info->VerticalResolution;
	g->stride = g_gop->Mode->Info->PixelsPerScanLine;
	g->front  = (UINT32 *)(UINTN)g_gop->Mode->FrameBufferBase;
	if (g->width == 0 || g->height == 0 || g->front == 0) {
		return -1;
	}

	/*
	 * Channel order. The firmware reports either a named layout or an
	 * explicit bitmask; both are reduced to a shift per channel so the rest
	 * of the renderer can stay in 0xAARRGGBB.
	 */
	switch (g_gop->Mode->Info->PixelFormat) {
	case PixelRedGreenBlueReserved8BitPerColor:
		g->shift_r = 0; g->shift_g = 8; g->shift_b = 16;
		break;
	case PixelBlueGreenRedReserved8BitPerColor:
		g->shift_r = 16; g->shift_g = 8; g->shift_b = 0;
		break;
	case PixelBitMask: {
		EFI_PIXEL_BITMASK m = g_gop->Mode->Info->PixelInformation;

		g->shift_r = (UINT8)__builtin_ctz(m.RedMask ? m.RedMask : 1);
		g->shift_g = (UINT8)__builtin_ctz(m.GreenMask ? m.GreenMask : 1);
		g->shift_b = (UINT8)__builtin_ctz(m.BlueMask ? m.BlueMask : 1);
		break;
	}
	default:
		return -1;      /* Blt-only mode: nothing to draw into */
	}

	g->back = 0;
	if (EFI_ERROR(status = g_st->BootServices->AllocatePool(2 /*EfiLoaderData*/,
	    (UINTN)g->stride * g->height * 4u, (void **)&g->back))) {
		return -1;
	}
	gfx_clear(g, 0xFF000000u);
	return 0;
}

void
gfx_present(struct gfx *g)
{
	UINT32 *dst = g->front;
	UINT32 *src = g->back;
	UINT32  n = g->width * g->height;
	UINT8   sr = g->shift_r, sg = g->shift_g, sb = g->shift_b;
	UINT32  y;

	for (y = 0; y < g->height; y++) {
		UINT32 *d = dst + (UINTN)y * g->stride;
		UINT32 *s = src + (UINTN)y * g->width;

		for (UINT32 x = 0; x < g->width; x++) {
			UINT32 c = s[x];

			d[x] = ((c >> 16 & 0xFF) << sr) |
			    ((c >> 8 & 0xFF) << sg) |
			    ((c & 0xFF) << sb);
		}
	}
}

void
gfx_clear(struct gfx *g, colour_t c)
{
	UINT32 *p = g->back;

	for (UINT32 i = 0; i < g->width * g->height; i++) {
		p[i] = c;
	}
}

void
gfx_pixel(struct gfx *g, INT32 x, INT32 y, colour_t c)
{
	UINT8 a = ALPHA(c);

	if (x < 0 || y < 0 || (UINT32)x >= g->width || (UINT32)y >= g->height) {
		return;
	}
	if (a == 0xFF) {
		g->back[(UINTN)y * g->width + x] = c;
		return;
	}
	if (a == 0) {
		return;
	}

	UINT32 *p = &g->back[(UINTN)y * g->width + x];
	UINT32  d = *p;
	UINT32  r = div255(((c >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * (255 - a));
	UINT32  gg = div255(((c >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * (255 - a));
	UINT32  b = div255((c & 0xFF) * a + (d & 0xFF) * (255 - a));

	*p = 0xFF000000u | (r << 16) | (gg << 8) | b;
}

void
gfx_fill(struct gfx *g, INT32 x, INT32 y, UINT32 w, UINT32 h, colour_t c)
{
	INT32 x0 = x < 0 ? 0 : x;
	INT32 y0 = y < 0 ? 0 : y;
	INT32 x1 = (INT32)(w + (UINT32)x) > (INT32)g->width ? (INT32)g->width : (INT32)(w + (UINT32)x);
	INT32 y1 = (INT32)(h + (UINT32)y) > (INT32)g->height ? (INT32)g->height : (INT32)(h + (UINT32)y);

	if (x1 <= x0 || y1 <= y0) {
		return;
	}
	if (ALPHA(c) == 0xFF) {
		for (INT32 j = y0; j < y1; j++) {
			UINT32 *row = &g->back[(UINTN)j * g->width];

			for (INT32 i = x0; i < x1; i++) {
				row[i] = c;
			}
		}
		return;
	}
	for (INT32 j = y0; j < y1; j++) {
		for (INT32 i = x0; i < x1; i++) {
			gfx_pixel(g, i, j, c);
		}
	}
}

void
gfx_vgradient(struct gfx *g, INT32 x, INT32 y, UINT32 w, UINT32 h,
    colour_t top, colour_t bottom)
{
	if (h == 0) {
		return;
	}
	for (UINT32 j = 0; j < h; j++) {
		/* 0..256 across the full height */
		UINT32 t = (UINT32)(((UINT64)j * 256u) / h);
		colour_t c = gfx_mix(top, bottom, t);

		gfx_fill(g, x, (INT32)((UINT32)y + j), w, 1, c);
	}
}

/*
 * Rounded rectangle via a coverage test per pixel, with the edge pixels
 * antialiased by comparing against a distance field. Radius 0 degenerates to
 * gfx_fill, so callers need no special case.
 */
static void
round_rect_impl(struct gfx *g, INT32 x, INT32 y, UINT32 w, UINT32 h,
    UINT32 radius, colour_t c, int outline, UINT32 thickness)
{
	INT32 r;
	INT32 x1, y1;

	if (w == 0 || h == 0) {
		return;
	}
	r = (INT32)radius;
	if (r > (INT32)(w / 2)) {
		r = (INT32)(w / 2);
	}
	if (r > (INT32)(h / 2)) {
		r = (INT32)(h / 2);
	}
	if (r <= 0) {
		if (outline) {
			gfx_fill(g, x, y, w, thickness, c);
			gfx_fill(g, x, (INT32)(y + h - thickness), w, thickness, c);
			gfx_fill(g, x, y, thickness, h, c);
			gfx_fill(g, (INT32)(x + w - thickness), y, thickness, h, c);
		} else {
			gfx_fill(g, x, y, w, h, c);
		}
		return;
	}

	x1 = (INT32)(x + w - 1);
	y1 = (INT32)(y + h - 1);

	for (INT32 j = y; j <= y1; j++) {
		if (j < 0 || (UINT32)j >= g->height) {
			continue;
		}
		for (INT32 i = x; i <= x1; i++) {
			if (i < 0 || (UINT32)i >= g->width) {
				continue;
			}
			/* distance from the nearest corner centre, if in a corner */
			INT32 cx = i, cy = j;
			int in_corner = 1;

			if (i < x + r) {
				cx = x + r;
			} else if (i > x1 - r) {
				cx = x1 - r;
			} else {
				in_corner = 0;
			}
			if (j < y + r) {
				cy = y + r;
			} else if (j > y1 - r) {
				cy = y1 - r;
			} else if (!in_corner) {
				/* straight middle band: fully inside */
				if (outline &&
				    i >= x + (INT32)thickness && i <= x1 - (INT32)thickness &&
				    j >= y + (INT32)thickness && j <= y1 - (INT32)thickness) {
					continue;
				}
				gfx_pixel(g, i, j, c);
				continue;
			}

			INT32 dx = i - cx, dy = j - cy;
			/* compare squared distance, scaled to keep 8-bit precision */
			INT32 d2 = dx * dx + dy * dy;
			INT32 outer = r * r;

			if (in_corner) {
				if (d2 > outer) {
					continue;       /* outside the rounded corner */
				}
			} else if (d2 > outer) {
				continue;
			}

			if (outline) {
				INT32 inner = (r - (INT32)thickness);

				if (inner < 0) {
					inner = 0;
				}
				if (in_corner) {
					if (d2 < inner * inner) {
						continue;       /* inside the hole */
					}
				} else if (i > x + (INT32)thickness &&
				    i < x1 - (INT32)thickness &&
				    j > y + (INT32)thickness &&
				    j < y1 - (INT32)thickness) {
					continue;
				}
			}
			gfx_pixel(g, i, j, c);
		}
	}
}

void
gfx_round_rect(struct gfx *g, INT32 x, INT32 y, UINT32 w, UINT32 h,
    UINT32 radius, colour_t c)
{
	round_rect_impl(g, x, y, w, h, radius, c, 0, 0);
}

void
gfx_round_outline(struct gfx *g, INT32 x, INT32 y, UINT32 w, UINT32 h,
    UINT32 radius, UINT32 thickness, colour_t c)
{
	round_rect_impl(g, x, y, w, h, radius, c, 1, thickness);
}

static const struct baked_glyph *
find_glyph(const struct baked_font *f, UINT16 cp)
{
	for (UINT32 i = 0; i < f->glyph_count; i++) {
		if (f->glyphs[i].codepoint == cp) {
			return &f->glyphs[i];
		}
	}
	return 0;
}

INT32
gfx_text(struct gfx *g, INT32 x, INT32 box_top, const char *s,
    const struct baked_font *f, colour_t c)
{
	UINT8  cr = (UINT8)((c >> 16) & 0xFF);
	UINT8  cg = (UINT8)((c >> 8) & 0xFF);
	UINT8  cb = (UINT8)(c & 0xFF);
	UINT8  ca = ALPHA(c);

	for (; *s != 0; s++) {
		const struct baked_glyph *gl = find_glyph(f, (UINT8)*s);

		if (gl == 0) {
			x += f->size / 2;
			continue;
		}
		if (gl->w != 0 && gl->h != 0) {
			INT32 gx = x + gl->offset_x;
			INT32 gy = box_top + gl->offset_y;

			for (UINT32 j = 0; j < gl->h; j++) {
				INT32 py = gy + (INT32)j;

				if (py < 0 || (UINT32)py >= g->height) {
					continue;
				}
				for (UINT32 i = 0; i < gl->w; i++) {
					UINT8 a = f->atlas[(UINTN)(gl->atlas_y + j) *
					    f->atlas_w + (gl->atlas_x + i)];

					if (a == 0) {
						continue;
					}
					/* fold the glyph coverage into the text alpha */
					UINT32 ea = (UINT32)a * ca / 255u;

					gfx_pixel(g, gx + (INT32)i, py,
					    ((colour_t)ea << 24) |
					    ((colour_t)cr << 16) |
					    ((colour_t)cg << 8) | cb);
				}
			}
		}
		x += gl->advance;
	}
	return x;
}

UINT32
gfx_text_width(const char *s, const struct baked_font *f)
{
	UINT32 w = 0;

	for (; *s != 0; s++) {
		const struct baked_glyph *gl = find_glyph(f, (UINT8)*s);

		w += gl != 0 ? gl->advance : f->size / 2;
	}
	return w;
}

void
gfx_text_centred(struct gfx *g, INT32 x, INT32 box_top, UINT32 w,
    const char *s, const struct baked_font *f, colour_t c)
{
	UINT32 tw = gfx_text_width(s, f);

	gfx_text(g, x + (INT32)((w - tw) / 2), box_top, s, f, c);
}
