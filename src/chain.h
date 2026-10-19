/*
 * chain.h -- chainloading another EFI image from the boot volume.
 *
 * This is the standard UEFI mechanism, not a reimplementation of anything:
 *
 *     LoadImage(TRUE, self_image, file_device_path, NULL, 0, &handle)
 *     StartImage(handle, NULL, NULL)
 *
 * LoadImage maps the PE image, applies relocations and resolves imports against
 * the boot services already in memory; StartImage enters its entry point. When
 * the loaded image returns, control comes back here -- which is what lets a boot
 * menu treat the launched program as an option rather than as a one-way jump.
 *
 * WHY THIS AND NOT A KERNEL JUMP. The ios-sweet kernel is not an EFI application:
 * it is a Mach-O payload that shim/loader.c copies into place and enters with the
 * MMU off. Doing that here would duplicate reviewed code and, worse, would mean
 * this launcher owned the segment copy and the MMU transition. What this launcher
 * CAN do correctly is what every boot menu does: start another EFI program. If the
 * shim is later built as an EFI application (or wrapped in one), it appears in
 * this menu and starts through this path with no changes here.
 */
#ifndef SWEET_UEFI_CHAIN_H
#define SWEET_UEFI_CHAIN_H

#include "efi.h"

/* Up to this many bootable .efi files are listed. */
#define CHAIN_MAX_ENTRIES 8

struct chain_entry {
	CHAR16 name[64];        /* file name as shown in the menu */
	UINT32 size;            /* image size in bytes            */
};

/*
 * Scan \sweet\ and the root of the boot volume for .efi images other than this
 * one. Returns the number found, 0 for none, negative on error.
 */
int chain_scan(EFI_HANDLE self, EFI_SYSTEM_TABLE *st,
    struct chain_entry *out, int max);

/*
 * Load and start entry `index` from the last chain_scan().
 * Returns 0 if the image ran and returned, negative on failure; the error text
 * is in chain_last_error().
 */
int chain_start(EFI_HANDLE self, EFI_SYSTEM_TABLE *st, int index);

const char *chain_last_error(void);

/* Device path of the file behind entry `index`, or NULL. Diagnostic only. */
const CHAR16 *chain_entry_path(int index);

#endif /* SWEET_UEFI_CHAIN_H */
