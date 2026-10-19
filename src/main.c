/*
 * main.c -- sweet-uefi: a graphical boot menu for the ios-sweet kernel port.
 *
 * What this is: a single freestanding UEFI application. It takes over the
 * framebuffer via GOP, draws a menu, and hands control to the XNU payload.
 *
 * Why an EFI application and not a "shim UEFI driver": the ios-sweet boot chain
 * is fastboot -> ABL -> whatever runs next. Putting this on the ESP as
 * \EFI\BOOT\BOOTAA64.EFI means the phone's normal boot picks it up with no
 * custom fastboot image, and it can offer more than one payload without
 * reflashing.
 *
 * The kernel handoff itself lives in payload.c; this file only does the menu,
 * input, and the timing of the animation.
 */

#include "efi.h"
#include "gfx.h"
#include "font.h"
#include "ui.h"
#include "payload.h"
#include "chain.h"

/* How long the menu waits before booting the default entry by itself. A phone
 * has no keyboard, so an unattended timeout is not a nicety -- without it the
 * device would sit on this screen forever. */
#define AUTO_BOOT_MS 5000u
#define FRAME_MS     16u      /* ~60 fps target; the timer below keeps it honest */

static EFI_SYSTEM_TABLE *ST;

static UINT64
now_ms(EFI_SYSTEM_TABLE *st)
{
	/*
	 * The monotonic counter ticks at 10 MHz per the UEFI spec (boot
	 * services GetNextMonotonicCount), which is what makes it usable for
	 * pacing without querying the timer's resolution.
	 */
	UINT64 ticks = 0;

	(void)st->BootServices->GetNextMonotonicCount(&ticks);
	return ticks / 10000u;
}

int
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
	static struct gfx g;
	static struct ui_state ui;
	EFI_STATUS status;
	EFI_INPUT_KEY key;
	UINT64 last, start;
	UINT32 action;
	int    rc;

	ST = st;
	ST->BootServices->SetWatchdogTimer(0, 0, 0, 0);

	die_init(ST);

	if (gfx_init(ST, &g) != 0) {
		die_text(ST, "No usable graphics output. This application needs a "
		    "GOP framebuffer; the firmware did not provide one.");
		return 1;
	}

	/* Find the payload and any bootable EFI images before building the menu. */
	payload_probe(image);

	static struct chain_entry chain[CHAIN_MAX_ENTRIES];
	int chain_count = chain_scan(image, ST, chain, CHAIN_MAX_ENTRIES);
	if (chain_count < 0) {
		chain_count = 0;
	}

	/* ---- menu contents ---- */
	ui.title    = "ios-sweet";
	ui.subtitle = payload_device_name();
	ui.status   = payload_status_line();

	ui.item_count = 0;

	/*
	 * Chained EFI images come first: those are the entries that actually run
	 * something. Labels are the file names because that is what the user put
	 * on the volume.
	 */
	for (int i = 0; i < chain_count && ui.item_count + 4 <= UI_MAX_ITEMS; i++) {
		static char detail[CHAIN_MAX_ENTRIES][48];
		UINT32 kb = (chain[i].size + 1023u) / 1024u;

		/* "1234 KB  EFI image" without printf */
		{
			char *d = detail[i];
			int   n = 0;
			char  tmp[12];
			int   t = 0;

			if (kb >= 10000u) {
				tmp[t++] = (char)('0' + (kb / 10000u) % 10u);
			}
			if (kb >= 1000u) {
				tmp[t++] = (char)('0' + (kb / 1000u) % 10u);
			}
			if (kb >= 100u) {
				tmp[t++] = (char)('0' + (kb / 100u) % 10u);
			}
			if (kb >= 10u) {
				tmp[t++] = (char)('0' + (kb / 10u) % 10u);
			}
			tmp[t++] = (char)('0' + kb % 10u);
			for (int k = 0; k < t && n < 44; k++) {
				d[n++] = tmp[k];
			}
			for (const char *sfx = " KB  EFI image"; *sfx && n < 46; sfx++) {
				d[n++] = *sfx;
			}
			d[n] = 0;
		}

		/* The UI wants char*, so narrow the UTF-16 name here. */
		static char labels[CHAIN_MAX_ENTRIES][64];
		{
			int n = 0;

			for (; chain[i].name[n] != 0 && n < 63; n++) {
				labels[i][n] = (char)(chain[i].name[n] & 0xFF);
			}
			labels[i][n] = 0;
		}
		ui.items[ui.item_count].label  = labels[i];
		ui.items[ui.item_count].detail = detail[i];
		ui.items[ui.item_count].kind   = UI_KIND_BOOT;
		ui.item_count++;
	}

	ui.items[ui.item_count].label  = "SWEETXNU payload";
	ui.items[ui.item_count].detail = payload_kernel_detail();
	ui.items[ui.item_count].kind   = UI_KIND_INFO;   /* the shim boots it */
	ui.item_count++;

	ui.items[ui.item_count].label  = "Boot options";
	ui.items[ui.item_count].detail = "boot-args and display mode";
	ui.items[ui.item_count].kind   = UI_KIND_INFO;
	ui.item_count++;

	ui.items[ui.item_count].label  = "Hardware";
	ui.items[ui.item_count].detail = "panel, SoC and memory as seen by the firmware";
	ui.items[ui.item_count].kind   = UI_KIND_INFO;
	ui.item_count++;

	ui.items[ui.item_count].label  = "Reboot";
	ui.items[ui.item_count].detail = 0;
	ui.items[ui.item_count].kind   = UI_KIND_INFO;
	ui.item_count++;

	ui.selected = 0;
	for (UINT32 i = 0; i < ui.item_count; i++) {
		if (ui.items[i].kind == UI_KIND_BOOT) {
			ui.selected = i;
			break;
		}
	}
	/* With nothing chainable, the countdown would pick a dead row; hold instead. */
	if (chain_count == 0) {
		ui.countdown_total_ms = 0;
	}

	ui.countdown_total_ms = AUTO_BOOT_MS;
	ui.countdown_ms = AUTO_BOOT_MS;

	start = now_ms(ST);
	last = start;

	for (;;) {
		UINT64 now = now_ms(ST);
		UINT32 elapsed = (UINT32)(now - last);

		if (elapsed < FRAME_MS) {
			/* Sleep the remainder rather than spin: a busy loop on a
			 * phone SoC means a hot device before the menu is even up. */
			ST->BootServices->Stall((FRAME_MS - elapsed) * 1000u);
			now = now_ms(ST);
			elapsed = (UINT32)(now - last);
		}
		last = now;

		/* ---- input ---- */
		while (ST->ConIn != 0 &&
		    !EFI_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &key))) {
			switch (key.UnicodeChar) {
			case 0:         /* no unicode: a scan code (arrows) */
				if (key.ScanCode == 0x01) {          /* up   */
					ui_move(&ui, -1);
					ui.countdown_ms = ui.countdown_total_ms;
				} else if (key.ScanCode == 0x02) {   /* down */
					ui_move(&ui, +1);
					ui.countdown_ms = ui.countdown_total_ms;
				}
				break;
			case 0x0D:      /* enter */
			case ' ':
				action = ui.selected;
				goto act;
			case 0x1B:      /* escape: hold the menu */
				ui.countdown_ms = 0;
				ui.countdown_total_ms = 0;
				break;
			default:
				ui.countdown_ms = ui.countdown_total_ms;
				break;
			}
		}

		/* ---- countdown ---- */
		if (ui.countdown_ms != 0) {
			ui.countdown_ms = elapsed >= ui.countdown_ms
			    ? 0 : ui.countdown_ms - elapsed;
		}

		action = ui_frame(&g, &ui, elapsed);

		if (action == UI_ACTION_TIMEOUT) {
			/* Time expired: boot the selected entry if it can boot. */
			action = ui.selected;
		}
		if (action != 0xFFFFFFFEu) {
			break;
		}
	}

act:
	if (action >= ui.item_count) {
		return 0;
	}
	if (ui.items[action].kind != UI_KIND_BOOT) {
		/* Info entries are not implemented yet; say so rather than
		 * pretending the key did nothing. */
		ui.status = "That panel is not implemented yet.";
		ui.countdown_ms = 0;
		ui.countdown_total_ms = 0;
		for (;;) {
			UINT64 now = now_ms(ST);

			if (ST->ConIn != 0 &&
			    !EFI_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &key))) {
				break;
			}
			ui_frame(&g, &ui, (UINT32)(now - last));
			last = now;
			ST->BootServices->Stall(16000);
		}
		return 0;
	}

	/* ---- run the selected entry ---- */
	if (action < (UINT32)chain_count) {
		/*
		 * A chained EFI image. If it returns, we get control back and drop
		 * to the firmware console with the reason -- which is more useful
		 * than silently re-drawing the menu.
		 */
		rc = chain_start(image, ST, (int)action);
		if (rc != 0) {
			die_text(ST, chain_last_error());
		}
		return 0;
	}

	rc = payload_start(ST, &g);
	if (rc != 0) {
		die_text(ST, payload_last_error());
	}
	return rc;
}
