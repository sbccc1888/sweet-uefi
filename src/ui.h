/*
 * ui.h -- the boot menu.
 */
#ifndef SWEET_UEFI_UI_H
#define SWEET_UEFI_UI_H

#include "efi.h"
#include "gfx.h"

/*
 * Sized for the worst case the launcher can produce: CHAIN_MAX_ENTRIES (8)
 * bootable images from the volume plus the four fixed rows the menu always
 * shows. It was 6, which both risked an overflow when eight images were found
 * and, because main.c reserves four slots for its fixed rows, silently limited
 * the menu to TWO bootable entries.
 */
#define UI_MAX_ITEMS 12

enum {
	UI_KIND_BOOT = 0,   /* the default action: start the kernel  */
	UI_KIND_INFO,       /* shows a panel instead of booting      */
	UI_KIND_DISABLED,   /* greyed out, cannot be selected        */
};

struct ui_item {
	const char *label;
	const char *detail;      /* small line under the label, may be 0 */
	int         kind;
};

struct ui_state {
	struct ui_item items[UI_MAX_ITEMS];
	UINT32  item_count;
	UINT32  selected;
	/* selection highlight position, animated towards `selected` */
	INT32   highlight_y;
	INT32   highlight_target_y;
	UINT32  countdown_ms;    /* remaining before auto-boot, 0 = none */
	UINT32  countdown_total_ms;
	const char *title;
	const char *subtitle;
	const char *status;      /* one line at the bottom, may be 0   */
	UINT32  anim_ms;         /* animation clock, milliseconds      */
};

/*
 * Lay the menu out and run one frame. Returns 0 to keep going, or the index of
 * the item to act on when the user chose one (or the countdown expired on the
 * UI_KIND_BOOT item, reported as UI_ACTION_TIMEOUT).
 */
#define UI_ACTION_TIMEOUT 0xFFFFFFFFu
UINT32 ui_frame(struct gfx *g, struct ui_state *s, UINT32 elapsed_ms);

/* Move the selection; clamps at both ends. */
void ui_move(struct ui_state *s, int delta);

#endif /* SWEET_UEFI_UI_H */
