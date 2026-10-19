/*
 * payload.h -- locating and starting the XNU payload.
 */
#ifndef SWEET_UEFI_PAYLOAD_H
#define SWEET_UEFI_PAYLOAD_H

#include "efi.h"
#include "gfx.h"

/*
 * Locate and validate the payload on the boot volume. Must be called once
 * before any of the accessors below, which only report what it found.
 */
void payload_probe(EFI_HANDLE image);

/* Device name for the subtitle, from the firmware's device tree if possible. */
const char *payload_device_name(void);

/* Menu detail line for the Start entry, e.g. the payload file and size. */
const char *payload_kernel_detail(void);

/* Status line: what was found, or what is missing. */
const char *payload_status_line(void);

/* 1 when a payload is present and looks loadable. */
int payload_kernel_available(void);

/*
 * Load the payload and transfer control. Only returns on failure, with
 * payload_last_error() describing why.
 */
int payload_start(EFI_SYSTEM_TABLE *st, struct gfx *g);

const char *payload_last_error(void);

/*
 * Last-resort output. If the graphical path failed there may be no framebuffer,
 * so this falls back to the firmware console.
 */
void die_init(EFI_SYSTEM_TABLE *st);
void die_text(EFI_SYSTEM_TABLE *st, const char *msg);

#endif /* SWEET_UEFI_PAYLOAD_H */
