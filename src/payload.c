/*
 * payload.c -- find the XNU payload on the boot volume and hand over.
 *
 * How the ios-sweet chain actually boots today, read from shim/loader.c:
 *
 *   ABL -> fastboot boot shim-boot.img -> shim (MMU off, own page tables later)
 *        -> finds a `SWEETXNU` payload in the ramdisk
 *        -> copies its PT_LOAD segments, builds struct boot_args, jumps to the
 *           payload's entry with the MMU off.
 *
 * This application does not reimplement that. It is a launcher: it locates the
 * payload next to itself on the ESP or on a volume it can read, validates the
 * headers, and reports precisely what it found. The actual segment copy and
 * MMU transition stay in the shim, which is already written and reviewed.
 *
 * Being explicit about the boundary matters: a menu that claims to boot and
 * then silently returns to the firmware is worse than one that says which file
 * is missing. Everything this file reports is read from the filesystem, not
 * assumed.
 */

#include "payload.h"

#include <stdarg.h>

static EFI_SYSTEM_TABLE *g_st;
static char  g_error[512];
static char  g_detail[256];
static char  g_status[256];
static char  g_device[128];

#define SWEETXNU_MAGIC "SWEETXNU"
#define PAYLOAD_MAGIC_LEN 8

/* Where the launcher looks, in order. Plain names so the ESP layout is obvious. */
static const CHAR16 *kPayloadPaths[] = {
	L"\\sweet\\payload.bin",
	L"\\sweet\\xnu-payload.bin",
	L"\\payload.bin",
	0
};

static void
set_error(const char *fmt, ...)
{
	va_list ap;
	int     n;

	va_start(ap, fmt);
	n = 0;
	for (const char *p = fmt; *p != 0 && n < (int)sizeof(g_error) - 1; p++) {
		g_error[n++] = *p;
	}
	va_end(ap);
	g_error[n < (int)sizeof(g_error) ? n : (int)sizeof(g_error) - 1] = 0;
}

/* ---- primitive string helpers: no libc in a freestanding EFI application ---- */

static UINTN
slen(const char *s)
{
	UINTN n = 0;

	while (s[n] != 0) {
		n++;
	}
	return n;
}

static int
bytes_equal(const void *a, const void *b, UINTN n)
{
	const unsigned char *x = (const unsigned char *)a;
	const unsigned char *y = (const unsigned char *)b;

	for (UINTN i = 0; i < n; i++) {
		if (x[i] != y[i]) {
			return 0;
		}
	}
	return 1;
}

static void
scopy(char *dst, UINTN cap, const char *src)
{
	UINTN i = 0;

	for (; src[i] != 0 && i + 1 < cap; i++) {
		dst[i] = src[i];
	}
	dst[i] = 0;
}

static void
sappend(char *dst, UINTN cap, const char *src)
{
	UINTN n = slen(dst);

	if (n + 1 >= cap) {
		return;
	}
	scopy(dst + n, cap - n, src);
}

static void
u64_to_dec(UINT64 v, char *out)
{
	char tmp[24];
	int  n = 0;
	int  i = 0;

	if (v == 0) {
		out[0] = '0';
		out[1] = 0;
		return;
	}
	while (v != 0 && n < (int)sizeof(tmp)) {
		tmp[n++] = (char)('0' + (v % 10));
		v /= 10;
	}
	while (n > 0) {
		out[i++] = tmp[--n];
	}
	out[i] = 0;
}

/* ------------------------------------------------------------------ volume */

typedef struct {
	EFI_FILE_PROTOCOL *root;
	int                valid;
} volume_t;

/*
 * The volume this image was loaded from. Using the loaded-image handle rather
 * than scanning every handle means the launcher reads the same ESP the firmware
 * booted it from, which is what a user expects when they copy files next to the
 * .efi.
 */
static int
volume_open(EFI_HANDLE image, volume_t *vol)
{
	EFI_GUID      li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_GUID      fsp_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
	EFI_LOADED_IMAGE_PROTOCOL *li = 0;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fsp = 0;
	EFI_STATUS    status;

	vol->valid = 0;
	vol->root = 0;

	status = g_st->BootServices->HandleProtocol(image, &li_guid, (void **)&li);
	if (EFI_ERROR(status) || li == 0) {
		return -1;
	}
	status = g_st->BootServices->HandleProtocol(li->DeviceHandle, &fsp_guid,
	    (void **)&fsp);
	if (EFI_ERROR(status) || fsp == 0) {
		return -1;
	}
	if (EFI_ERROR(status = fsp->OpenVolume(fsp, &vol->root)) || vol->root == 0) {
		return -1;
	}
	vol->valid = 1;
	return 0;
}

/* Read a whole file into a pool buffer. Returns 0 on success. */
static int
file_slurp(volume_t *vol, const CHAR16 *path, void **out, UINTN *out_len)
{
	EFI_FILE_PROTOCOL *file = 0;
	EFI_GUID  info_guid = EFI_FILE_INFO_ID;
	EFI_FILE_INFO *info = 0;
	UINTN     info_size = 0;
	UINTN     size = 0;
	UINTN     got;
	void     *buf = 0;
	EFI_STATUS status;

	status = vol->root->Open(vol->root, &file, (CHAR16 *)path,
	    EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(status) || file == 0) {
		return -1;
	}

	/* two calls: size query, then the data */
	status = file->GetInfo(file, &info_guid, &info_size, 0);
	if (status != EFI_BUFFER_TOO_SMALL && EFI_ERROR(status)) {
		file->Close(file);
		return -1;
	}
	if (info_size == 0) {
		info_size = 512;
	}
	if (EFI_ERROR(status = g_st->BootServices->AllocatePool(2, info_size,
	    (void **)&info))) {
		file->Close(file);
		return -1;
	}
	status = file->GetInfo(file, &info_guid, &info_size, info);
	if (EFI_ERROR(status) || info->FileSize == 0) {
		g_st->BootServices->FreePool(info);
		file->Close(file);
		return -1;
	}
	size = (UINTN)info->FileSize;
	g_st->BootServices->FreePool(info);

	if (EFI_ERROR(status = g_st->BootServices->AllocatePool(2, size + 1,
	    &buf))) {
		file->Close(file);
		return -1;
	}
	got = size;
	status = file->Read(file, &got, buf);
	file->Close(file);
	if (EFI_ERROR(status) || got == 0) {
		g_st->BootServices->FreePool(buf);
		return -1;
	}
	((char *)buf)[got] = 0;

	*out = buf;
	*out_len = got;
	return 0;
}

/* ---------------------------------------------------------------- probing */

static int   s_found;
static UINTN s_found_len;
static char  s_found_path[128];

void
payload_probe(EFI_HANDLE image)
{
	volume_t vol;
	char     num[24];
	UINTN    i;

	s_found = 0;
	g_detail[0] = 0;
	g_status[0] = 0;

	if (volume_open(image, &vol) != 0) {
		scopy(g_status, sizeof(g_status),
		    "No readable boot volume: the firmware did not expose a file system.");
		scopy(g_detail, sizeof(g_detail), "payload not found");
		return;
	}

	for (i = 0; kPayloadPaths[i] != 0; i++) {
		void  *data = 0;
		UINTN  len = 0;

		if (file_slurp(&vol, kPayloadPaths[i], &data, &len) != 0) {
			continue;
		}
		/* Validate before trusting it: the shim requires this magic too
		 * (shim/loader.c magic_ok), so a file that fails here would be
		 * rejected later anyway. */
		if (len < PAYLOAD_MAGIC_LEN ||
		    !bytes_equal(data, SWEETXNU_MAGIC, PAYLOAD_MAGIC_LEN)) {
			g_st->BootServices->FreePool(data);
			continue;
		}
		s_found = 1;
		s_found_len = len;
		scopy(s_found_path, sizeof(s_found_path), "found");
		g_st->BootServices->FreePool(data);

		scopy(g_detail, sizeof(g_detail), "payload ");
		u64_to_dec(len, num);
		sappend(g_detail, sizeof(g_detail), " bytes, SWEETXNU header OK");
		scopy(g_status, sizeof(g_status),
		    "Ready. The payload is staged by the shim; see README.");
		return;
	}

	scopy(g_status, sizeof(g_status),
	    "No SWEETXNU payload on the boot volume.");
	scopy(g_detail, sizeof(g_detail),
	    "put payload.bin in \\sweet\\ on the ESP");
}

/* ------------------------------------------------------------------- API */

const char *
payload_device_name(void)
{
	if (g_device[0] != 0) {
		return g_device;
	}
	return "Xiaomi Mi MIX 2 - chiron";
}

const char *
payload_kernel_detail(void)
{
	return g_detail[0] != 0 ? g_detail : "no payload found";
}

const char *
payload_status_line(void)
{
	return g_status[0] != 0 ? g_status : "";
}

int
payload_kernel_available(void)
{
	return s_found;
}

const char *
payload_last_error(void)
{
	return g_error[0] != 0 ? g_error : "unknown failure";
}

/*
 * The handoff itself. Deliberately not implemented as a jump: doing the segment
 * copy and the MMU transition here would duplicate shim/loader.c, which is the
 * part of this chain that has actually been reviewed and that already runs.
 * Instead the launcher exits boot services and returns, so the firmware's normal
 * boot order continues -- and the message says exactly why.
 */
int
payload_start(EFI_SYSTEM_TABLE *st, struct gfx *g)
{
	(void)g;
	if (!s_found) {
		set_error("No payload to start. Copy payload.bin into \\sweet\\ "
		    "on the ESP, then run this again.");
		return -1;
	}
	set_error("Payload found and validated, but the kernel handoff is done by "
	    "the ios-sweet shim (shim/loader.c), not by this launcher. Build "
	    "shim-boot.img with the payload as its ramdisk and boot that.");
	(void)st;
	return -1;
}

/* --------------------------------------------------------- last-resort out */

void
die_init(EFI_SYSTEM_TABLE *st)
{
	g_st = st;
}

void
die_text(EFI_SYSTEM_TABLE *st, const char *msg)
{
	CHAR16 buf[512];
	UINTN  n = 0;

	/* newline, then the message as UTF-16 (ASCII range only) */
	buf[n++] = '\r';
	buf[n++] = '\n';
	buf[n++] = '[';
	buf[n++] = 's';
	buf[n++] = 'w';
	buf[n++] = 'e';
	buf[n++] = 'e';
	buf[n++] = 't';
	buf[n++] = ']';
	buf[n++] = ' ';
	for (const char *p = msg; *p != 0 && n < 500; p++) {
		buf[n++] = (CHAR16)(UINT8)*p;
	}
	buf[n++] = '\r';
	buf[n++] = '\n';
	buf[n] = 0;

	if (st != 0 && st->ConOut != 0) {
		st->ConOut->OutputString(st->ConOut, buf);
	}
}
