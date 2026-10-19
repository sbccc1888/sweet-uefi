/*
 * ui.c -- the boot menu.
 *
 * Layout is proportional, not fixed: the panel on these phones is 1080x2160 but
 * the same binary may run on a tablet or a small GOP window, so every
 * dimension derives from the framebuffer size. That also means the menu fills a
 * phone screen properly instead of sitting in a corner.
 */

#include "ui.h"

/* Palette. Dark, cool, low-contrast background so the panel's blacks stay deep. */
#define C_BG_TOP      0xFF0A0E1Au
#define C_BG_BOTTOM   0xFF141A2Eu
#define C_CARD        0xFF1B2138u
#define C_CARD_EDGE   0xFF2A3350u
#define C_TITLE       0xFFF2F5FFu
#define C_SUBTITLE    0xFF8A94B8u
#define C_ITEM        0xFFD8DEF0u
#define C_ITEM_DETAIL 0xFF757FA3u
#define C_ACCENT      0xFF4C8DFFu
#define C_ACCENT_DIM  0xFF2F5FAFu
#define C_HINT        0xFF5A6486u
#define C_DISABLED    0xFF454E6Au
#define C_SHADOW      0x50000000u
#define C_TRACK       0xFF232B45u

void
ui_move(struct ui_state *s, int delta)
{
	INT32 next = (INT32)s->selected;
	UINT32 guard = 0;

	if (s->item_count == 0) {
		return;
	}
	do {
		next += delta;
		if (next < 0) {
			next = (INT32)s->item_count - 1;
		}
		if (next >= (INT32)s->item_count) {
			next = 0;
		}
		if (++guard > s->item_count * 2) {
			return;         /* everything is disabled */
		}
	} while (s->items[next].kind == UI_KIND_DISABLED);
	s->selected = (UINT32)next;
}

UINT32
ui_frame(struct gfx *g, struct ui_state *s, UINT32 elapsed_ms)
{
	UINT32 W = g->width, H = g->height;
	UINT32 margin, card_w, card_x, row_h, row_gap, card_y, card_h;
	UINT32 pad;
	UINT32 action = 0xFFFFFFFEu;    /* "keep going" */

	s->anim_ms += elapsed_ms;

	/* ---- background ---- */
	gfx_vgradient(g, 0, 0, W, H, C_BG_TOP, C_BG_BOTTOM);

	/*
	 * A soft accent bloom behind the title, drawn as concentric translucent
	 * circles. Cheap, and it stops the background from looking like a flat
	 * rectangle of colour.
	 */
	/*
	 * Note: the accent rule is drawn with the title block further down, not
	 * here. Its position has to follow the title, and the title's position
	 * depends on the card height, which is not known until the layout block.
	 */

	/* ---- layout ---- */
	margin = W / 14;
	card_w = W - 2 * margin;
	card_x = margin;
	pad    = (W / 100) * 6;

	/*
	 * Row height from the two text lines that actually go in it, plus a fixed
	 * gap -- not a fraction of the screen. On a tall panel a percentage row
	 * height wastes the space; on a small one it overflows.
	 */
	{
		UINT32 two_lines = g_fonts[FONT_BODY].line_height +
		    g_fonts[FONT_SMALL].line_height;

		row_h = two_lines + pad;
		row_gap = pad / 3;
	}
	card_h = (UINT32)(s->item_count * row_h +
	    (s->item_count - 1) * row_gap) + pad * 2;

	/*
	 * If the list is too tall for the panel, shrink the ROWS, not just the
	 * card. Clamping card_h on its own leaves the rows drawing past its bottom
	 * edge, because their positions are computed from row_h. A phone with
	 * several bootable images is exactly the case that reaches this.
	 */
	if (card_h > H - H / 5) {
		UINT32 limit = H - H / 5;
		UINT32 fixed = pad * 2 + (s->item_count - 1) * row_gap;
		UINT32 avail = limit > fixed ? limit - fixed : 0;
		UINT32 two_lines = g_fonts[FONT_BODY].line_height +
		    g_fonts[FONT_SMALL].line_height;

		if (s->item_count > 0) {
			row_h = avail / s->item_count;
		}
		if (row_h < two_lines + 4) {
			/* Text must still fit inside the highlight pill. */
			row_h = two_lines + 4;
			row_gap = 2;
		}
		card_h = (UINT32)(s->item_count * row_h +
		    (s->item_count - 1) * row_gap) + pad * 2;
		if (card_h > H) {
			card_h = H;
		}
	}
	card_y = (card_h < H) ? ((H - card_h) / 2 + H / 28) : 0;
	if (card_y + card_h > H) {
		card_y = H > card_h ? H - card_h : 0;
	}

	/* ---- shadow + card ---- */
	gfx_round_rect(g, (INT32)card_x + 4, (INT32)card_y + 10, card_w, card_h,
	    pad, C_SHADOW);
	gfx_round_rect(g, (INT32)card_x, (INT32)card_y, card_w, card_h, pad, C_CARD);
	gfx_round_outline(g, (INT32)card_x, (INT32)card_y, card_w, card_h, pad, 1,
	    C_CARD_EDGE);

	/* ---- title block ---- */
	{
		UINT32 title_y = card_y - pad - (UINT32)(H / 100) * 16;

		/*
		 * The accent rule is positioned from the title, not from a fraction of
		 * the screen. With a fixed H/8 the rule stayed put while a taller card
		 * lifted the title, and it ended up under the subtitle looking like a
		 * stray underline -- which is exactly what an eight-entry menu produced.
		 */
		{
			UINT32 rule_w = W / 9;
			UINT32 rule_x = (W - rule_w) / 2;
			UINT32 gap   = (UINT32)(H / 40) + 24;
			UINT32 rule_y = title_y > gap ? title_y - gap : 8;

			gfx_round_rect(g, (INT32)rule_x, (INT32)rule_y, rule_w, 5, 3,
			    C_ACCENT);
			gfx_round_rect(g, (INT32)(rule_x + rule_w / 3),
			    (INT32)(rule_y + 20), rule_w / 3, 4, 2, C_ACCENT_DIM);
		}

		gfx_text_centred(g, 0, (INT32)title_y,
		    W, s->title, &g_fonts[FONT_TITLE], C_TITLE);
		if (s->subtitle != 0) {
			UINT32 sub_y = title_y + g_fonts[FONT_TITLE].line_height +
			    (UINT32)(H / 200);

			gfx_text_centred(g, 0, (INT32)sub_y, W,
			    s->subtitle, &g_fonts[FONT_BODY], C_SUBTITLE);
		}
	}

	/* ---- rows ---- */
	{
		UINT32 row_top = card_y + pad;
		UINT32 inset = pad / 2;
		UINT32 hl_h = row_h;

		s->highlight_target_y = (INT32)(row_top +
		    s->selected * (row_h + row_gap));

		/* ease the highlight towards its target */
		if (s->highlight_y == 0) {
			s->highlight_y = s->highlight_target_y;    /* first frame */
		} else {
			INT32 diff = s->highlight_target_y - s->highlight_y;

			s->highlight_y += diff / 3 + (diff > 0 ? 1 : (diff < 0 ? -1 : 0));
		}

		/* the moving pill, drawn under the text */
		gfx_round_rect(g, (INT32)(card_x + inset), s->highlight_y,
		    card_w - 2 * inset, hl_h, hl_h / 2, C_ACCENT);

		for (UINT32 i = 0; i < s->item_count; i++) {
			UINT32 y = row_top + i * (row_h + row_gap);
			int    sel = (i == s->selected);
			colour_t lc, dc;

			if (s->items[i].kind == UI_KIND_DISABLED) {
				lc = dc = C_DISABLED;
			} else if (sel) {
				lc = C_TITLE;
				dc = RGBA(0xFF, 0xFF, 0xFF, 0xB0);
			} else {
				lc = C_ITEM;
				dc = C_ITEM_DETAIL;
			}

			/*
			 * Measure the block, then centre it: deriving the two
			 * baselines independently is what pushed the detail line
			 * out of the highlight pill.
			 */
			const struct baked_font *lf = &g_fonts[FONT_BODY];
			const struct baked_font *df = &g_fonts[FONT_SMALL];
			UINT32 block_h = lf->line_height +
			    (s->items[i].detail != 0 ? df->line_height : 0);
			INT32  top = (INT32)(y + (row_h - block_h) / 2);

			gfx_text(g, (INT32)(card_x + pad), top,
			    s->items[i].label, lf, lc);

			if (s->items[i].detail != 0) {
				gfx_text(g, (INT32)(card_x + pad),
				    top + (INT32)lf->line_height,
				    s->items[i].detail, df, dc);
			}
		}
	}

	/* ---- countdown bar ---- */
	if (s->countdown_total_ms != 0) {
		UINT32 bar_w = card_w - 2 * pad;
		UINT32 bar_x = card_x + pad;
		UINT32 bar_y = card_y + card_h + pad;
		UINT32 fill;
		char   buf[64];
		UINT32 secs = (s->countdown_ms + 999) / 1000;

		gfx_round_rect(g, (INT32)bar_x, (INT32)bar_y, bar_w, 6, 3, C_TRACK);
		fill = bar_w - (UINT32)((UINT64)bar_w * s->countdown_ms /
		    (s->countdown_total_ms ? s->countdown_total_ms : 1));
		if (fill > 0) {
			gfx_round_rect(g, (INT32)bar_x, (INT32)bar_y, fill, 6, 3,
			    C_ACCENT);
		}

		/* "Booting in N" under the bar, hand-formatted: no printf here */
		{
			const char *pre = "Booting in ";
			const char *suf = " s   (press any key to hold)";
			UINT32 n = 0;

			for (const char *p = pre; *p; p++) {
				buf[n++] = *p;
			}
			if (secs >= 10) {
				buf[n++] = (char)('0' + (secs / 10) % 10);
			}
			buf[n++] = (char)('0' + secs % 10);
			for (const char *p = suf; *p; p++) {
				buf[n++] = *p;
			}
			buf[n] = 0;

			gfx_text_centred(g, 0, (INT32)(bar_y + 20), W, buf,
			    &g_fonts[FONT_SMALL], C_HINT);
		}

		if (s->countdown_ms == 0) {
			action = UI_ACTION_TIMEOUT;
		}
	}

	/* ---- status line ---- */
	if (s->status != 0) {
		gfx_text_centred(g, 0, (INT32)(H - margin),
		    W, s->status, &g_fonts[FONT_SMALL], C_HINT);
	}

	gfx_present(g);
	return action;
}
