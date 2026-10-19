/*
 * chain.c -- chainload .efi images from the boot volume.
 *
 * See chain.h for why this is LoadImage/StartImage rather than a kernel jump.
 *
 * The device path is assembled the way edk2's FileDevicePath() does it: take the
 * device path of the handle the file system lives on, drop its END node, append a
 * MEDIA_FILEPATH node carrying the file name in UTF-16, then a new END node.
 * LoadImage resolves that through the Simple File System protocol itself.
 *
 * Directory enumeration uses the standard pattern from UEFI 2.10 13.5.5: query
 * the EFI_FILE_INFO size with GetInfo, allocate that once, then call Read
 * repeatedly; Read reports 0 bytes when the directory is exhausted.
 */

#include "chain.h"

static char g_error[192];

static void
set_error(const char *msg)
{
	int i = 0;

	for (; msg[i] != 0 && i < (int)sizeof(g_error) - 1; i++) {
		g_error[i] = msg[i];
	}
	g_error[i] = 0;
}

const char *
chain_last_error(void)
{
	return g_error[0] ? g_error : "unknown failure";
}

/* ------------------------------------------------------------------ paths */

static UINTN
wlen(const CHAR16 *s)
{
	UINTN n = 0;

	while (s[n] != 0) {
		n++;
	}
	return n;
}

static void
wcopy(CHAR16 *dst, UINTN cap, const CHAR16 *src)
{
	UINTN i = 0;

	for (; src[i] != 0 && i + 1 < cap; i++) {
		dst[i] = src[i];
	}
	dst[i] = 0;
}

/* <volume device path> + FILEPATH(name) + END. Caller frees. */
static EFI_DEVICE_PATH_PROTOCOL *
build_file_device_path(EFI_SYSTEM_TABLE *st, EFI_HANDLE device,
    const CHAR16 *name)
{
	EFI_GUID dp_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
	EFI_DEVICE_PATH_PROTOCOL *vol = 0;
	EFI_DEVICE_PATH_PROTOCOL *out = 0;
	UINT8 *p;
	UINTN  vol_len = 0;
	UINTN  name_chars, node_len, total;
	FILEPATH_DEVICE_PATH *fp;
	UINT8 *end;
	EFI_STATUS status;

	if (EFI_ERROR(st->BootServices->HandleProtocol(device, &dp_guid,
	    (void **)&vol)) || vol == 0) {
		return 0;
	}

	/* Find the END/END_ENTIRE node to measure the volume path. */
	p = (UINT8 *)vol;
	for (UINTN guard = 0; guard < 64; guard++) {
		EFI_DEVICE_PATH_PROTOCOL *n = (EFI_DEVICE_PATH_PROTOCOL *)p;
		UINTN len = (UINTN)n->Length[0] | ((UINTN)n->Length[1] << 8);

		if (len < 4) {
			return 0;
		}
		p += len;
		if (n->Type == DEVICE_PATH_TYPE_END &&
		    n->SubType == END_SUBTYPE_ENTIRE) {
			vol_len = (UINTN)(p - (UINT8 *)vol);
			break;
		}
	}
	if (vol_len < 4) {
		return 0;
	}
	vol_len -= 4;                       /* replace the END node */

	name_chars = wlen(name) + 1;
	node_len = 4 + name_chars * sizeof(CHAR16);
	total = vol_len + node_len + 4;

	if (EFI_ERROR(status = st->BootServices->AllocatePool(EFI_LOADER_DATA,
	    total, (void **)&out)) || out == 0) {
		return 0;
	}

	p = (UINT8 *)out;
	for (UINTN i = 0; i < vol_len; i++) {
		p[i] = ((UINT8 *)vol)[i];
	}
	fp = (FILEPATH_DEVICE_PATH *)(p + vol_len);
	fp->Header.Type = DEVICE_PATH_TYPE_MEDIA;
	fp->Header.SubType = MEDIA_SUBTYPE_FILEPATH;
	fp->Header.Length[0] = (UINT8)(node_len & 0xFF);
	fp->Header.Length[1] = (UINT8)((node_len >> 8) & 0xFF);
	for (UINTN i = 0; i < name_chars; i++) {
		fp->PathName[i] = name[i];
	}
	end = (UINT8 *)fp + node_len;
	end[0] = DEVICE_PATH_TYPE_END;
	end[1] = END_SUBTYPE_ENTIRE;
	end[2] = 4;
	end[3] = 0;
	return out;
}

/* ---------------------------------------------------------------- state */

struct found {
	CHAR16 path[96];
	CHAR16 name[64];
	UINT32 size;
};

static struct found g_found[CHAIN_MAX_ENTRIES];
static int          g_count;
static EFI_HANDLE   g_volume_handle;

const CHAR16 *
chain_entry_path(int index)
{
	if (index < 0 || index >= g_count) {
		return 0;
	}
	return g_found[index].path;
}

/* Case-insensitive ".efi" suffix test. */
static int
ends_with_efi(const CHAR16 *s)
{
	UINTN n = wlen(s);
	CHAR16 c[4];

	if (n < 4) {
		return 0;
	}
	for (int i = 0; i < 4; i++) {
		CHAR16 v = s[n - 4 + i];

		c[i] = (v >= 'A' && v <= 'Z') ? (CHAR16)(v + 32) : v;
	}
	return c[0] == '.' && c[1] == 'e' && c[2] == 'f' && c[3] == 'i';
}

static void
add_entry(const CHAR16 *prefix, const CHAR16 *name, UINT32 size)
{
	struct found *e;
	UINTN n = 0;

	if (g_count >= CHAIN_MAX_ENTRIES || ends_with_efi(name) == 0) {
		return;
	}
	e = &g_found[g_count];
	if (prefix != 0) {
		for (UINTN i = 0; prefix[i] != 0 && n + 1 < 96; i++) {
			e->path[n++] = prefix[i];
		}
	}
	for (UINTN i = 0; name[i] != 0 && n + 1 < 96; i++) {
		e->path[n++] = name[i];
	}
	e->path[n] = 0;
	wcopy(e->name, 64, name);
	e->size = size;
	g_count++;
}

/*
 * Enumerate `dir` (a path like L"\\sweet\\") and collect .efi names.
 * Returns 0 on success even when the directory is empty or absent.
 */
static int
scan_dir(EFI_SYSTEM_TABLE *st, EFI_FILE_PROTOCOL *root, const CHAR16 *dir,
    const CHAR16 *prefix)
{
	EFI_GUID info_guid = EFI_FILE_INFO_ID;
	EFI_FILE_PROTOCOL *d = 0;
	EFI_FILE_INFO *info = 0;
	UINTN  info_size = 0;
	EFI_STATUS status;

	if (dir != 0) {
		status = root->Open(root, &d, (CHAR16 *)dir, EFI_FILE_MODE_READ, 0);
		if (EFI_ERROR(status) || d == 0) {
			return 0;               /* absent is not an error */
		}
	} else {
		d = root;
	}

	/* Query the size EFI_FILE_INFO needs for the largest name (spec 13.5.6). */
	status = d->GetInfo(d, &info_guid, &info_size, 0);
	if (status != EFI_BUFFER_TOO_SMALL && EFI_ERROR(status)) {
		if (d != root) {
			d->Close(d);
		}
		return 0;
	}
	if (info_size == 0) {
		info_size = 1024;
	}
	if (EFI_ERROR(st->BootServices->AllocatePool(EFI_LOADER_DATA, info_size,
	    (void **)&info)) || info == 0) {
		if (d != root) {
			d->Close(d);
		}
		return -1;
	}

	for (UINTN guard = 0; guard < 256; guard++) {
		UINTN got = info_size;

		if (EFI_ERROR(d->Read(d, &got, info)) || got == 0) {
			break;                  /* end of directory */
		}
		/* Skip "." and ".." and the size of a subdirectory is irrelevant. */
		if (info->FileName[0] == '.' &&
		    (info->FileName[1] == 0 ||
		    (info->FileName[1] == '.' && info->FileName[2] == 0))) {
			continue;
		}
		if (info->Attribute & 0x10u) {   /* EFI_FILE_DIRECTORY */
			continue;
		}
		add_entry(prefix, info->FileName, (UINT32)info->FileSize);
	}

	st->BootServices->FreePool(info);
	if (d != root) {
		d->Close(d);
	}
	return 0;
}

/*
 * Shift one entry out. Written out rather than a struct assignment because the
 * latter compiles to a memcpy call, and there is no libc in an EFI application
 * -- the linker rejects it.
 */
static void
remove_entry(int index)
{
	if (index < 0 || index >= g_count) {
		return;
	}
	for (int j = index; j + 1 < g_count; j++) {
		for (int k = 0; k < 96; k++) {
			g_found[j].path[k] = g_found[j + 1].path[k];
			if (k < 64) {
				g_found[j].name[k] = g_found[j + 1].name[k];
			}
		}
		g_found[j].size = g_found[j + 1].size;
	}
	g_count--;
}
int
chain_scan(EFI_HANDLE self, EFI_SYSTEM_TABLE *st,
    struct chain_entry *out, int max)
{
	EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
	EFI_LOADED_IMAGE_PROTOCOL *li = 0;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
	EFI_FILE_PROTOCOL *root = 0;
	EFI_STATUS status;

	g_count = 0;
	g_error[0] = 0;

	status = st->BootServices->HandleProtocol(self, &li_guid, (void **)&li);
	if (EFI_ERROR(status) || li == 0) {
		set_error("cannot read this image's loaded-image protocol");
		return -1;
	}
	g_volume_handle = li->DeviceHandle;

	status = st->BootServices->HandleProtocol(g_volume_handle, &fs_guid,
	    (void **)&fs);
	if (EFI_ERROR(status) || fs == 0) {
		set_error("the boot volume has no file system protocol");
		return -1;
	}
	if (EFI_ERROR(status = fs->OpenVolume(fs, &root)) || root == 0) {
		set_error("cannot open the boot volume");
		return -1;
	}

	/* \sweet\ first, then the root: the documented layout before the spare. */
	(void)scan_dir(st, root, L"sweet\\", L"sweet\\");
	(void)scan_dir(st, root, 0, 0);

	/*
	 * Drop our own image from the list. Launching the menu from the menu is
	 * not a useful option and would recurse.
	 */
	if (li->FilePath != 0) {
		const CHAR16 *own = 0;
		FILEPATH_DEVICE_PATH *fp = (FILEPATH_DEVICE_PATH *)li->FilePath;

		if (fp->Header.Type == DEVICE_PATH_TYPE_MEDIA &&
		    fp->Header.SubType == MEDIA_SUBTYPE_FILEPATH) {
			own = fp->PathName;
		}
		if (own != 0) {
			/* Compare only the file name: the volume prefix differs. */
			UINTN on = wlen(own);
			const CHAR16 *base = own;

			for (UINTN i = 0; i < on; i++) {
				if (own[i] == '\\' || own[i] == '/') {
					base = own + i + 1;
				}
			}
			for (int i = 0; i < g_count; i++) {
				UINTN a = wlen(g_found[i].name), b = wlen(base);
				int same = (a == b);

				for (UINTN k = 0; same && k < a; k++) {
					CHAR16 x = g_found[i].name[k];
					CHAR16 y = base[k];

					if (x >= 'A' && x <= 'Z') {
						x = (CHAR16)(x + 32);
					}
					if (y >= 'A' && y <= 'Z') {
						y = (CHAR16)(y + 32);
					}
					same = (x == y);
				}
				if (same) {
					remove_entry(i);
					break;
				}
			}
		}
	}

	for (int i = 0; i < g_count && out != 0 && i < max; i++) {
		wcopy(out[i].name, (UINTN)(sizeof(out[i].name) / sizeof(CHAR16)),
		    g_found[i].name);
		out[i].size = g_found[i].size;
	}
	return g_count;
}

int
chain_start(EFI_HANDLE self, EFI_SYSTEM_TABLE *st, int index)
{
	EFI_DEVICE_PATH_PROTOCOL *dp;
	EFI_HANDLE image = 0;
	UINTN exit_size = 0;
	CHAR16 *exit_data = 0;
	EFI_STATUS status;

	if (index < 0 || index >= g_count) {
		set_error("no such boot option");
		return -1;
	}

	dp = build_file_device_path(st, g_volume_handle, g_found[index].path);
	if (dp == 0) {
		set_error("cannot build a device path for that file");
		return -1;
	}

	/*
	 * LoadImage with SourceBufferSize 0 means "read it from the device path",
	 * which is what we want: the file is already on the volume and mapping it
	 * twice would waste pool.
	 */
	status = st->BootServices->LoadImage(FALSE, self, dp, 0, 0, &image);
	st->BootServices->FreePool(dp);
	if (EFI_ERROR(status) || image == 0) {
		set_error("LoadImage failed: not a loadable EFI image?");
		return -1;
	}

	status = st->BootServices->StartImage(image, &exit_size, &exit_data);
	if (EFI_ERROR(status)) {
		/*
		 * Leave the image loaded on failure: unloading something that
		 * half-ran is worse than leaving it, and the firmware will reclaim
		 * it on reset.
		 */
		set_error("StartImage failed: the image returned an error");
		return -1;
	}

	/* The image ran and returned normally. It stays loaded; that is fine. */
	return 0;
}
