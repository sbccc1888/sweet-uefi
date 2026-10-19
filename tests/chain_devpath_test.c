/*
 * chain_devpath_test.c -- host test of the chainloader's device path build.
 *
 * Why: chain.c's build_file_device_path() does pointer arithmetic over a
 * variable-length on-disk structure. Get it wrong and LoadImage is handed a
 * malformed path; the firmware either rejects it or, worse, walks past the end of
 * the buffer. Nothing about that fails to compile. Same class of bug as the touch
 * driver's STOP bit, which a host test caught.
 *
 * How: chain.c is compiled unchanged against the REAL efi.h -- so the structures
 * under test are the ones that ship -- with just the boot services it calls
 * stubbed out. The test then walks the produced path node by node and checks the
 * invariants UEFI 2.10 chapter 10.3 requires:
 *
 *   - every node's Length includes its own 4-byte header and is >= 4
 *   - the walk terminates on an END/END_ENTIRE node, exactly once
 *   - nodes exactly tile the buffer: no gap, no overrun
 *   - the volume prefix is preserved byte for byte, minus its END node
 *   - the FILEPATH node carries the name in UTF-16, NUL-terminated
 *   - a maximum-length name still produces a well-formed path
 *
 * Built and run by tools/test-chain.sh; never part of the EFI image.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "efi.h"

static int g_failures;
static int g_checks;

#define CHECK(cond, ...)                                            \
	do {                                                        \
		g_checks++;                                         \
		if (!(cond)) {                                      \
			g_failures++;                               \
			printf("  FAIL  " __VA_ARGS__);            \
			printf("\n");                               \
		}                                                   \
	} while (0)

/*
 * A synthetic volume device path: one MESSAGING node with 8 bytes of payload,
 * then END. The content is arbitrary; what matters is that the builder preserves
 * it byte for byte and replaces only the trailing END node.
 */
static UINT8 g_vol[32];
static UINTN g_vol_len;

static void
make_volume_path(void)
{
	g_vol[0] = 0x03;            /* MESSAGING */
	g_vol[1] = 0x05;
	g_vol[2] = 12;              /* length */
	g_vol[3] = 0;
	for (int i = 4; i < 12; i++) {
		g_vol[i] = (UINT8)(0xA0 + i);
	}
	g_vol[12] = 0x7F;           /* END */
	g_vol[13] = 0xFF;           /* END_ENTIRE */
	g_vol[14] = 4;
	g_vol[15] = 0;
	g_vol_len = 16;
}

static EFI_STATUS
stub_allocate_pool(UINT32 type, UINTN size, void **out)
{
	(void)type;
	*out = calloc(1, (size_t)size);
	return *out != NULL ? EFI_SUCCESS : 1;
}

static EFI_STATUS
stub_free_pool(void *p)
{
	free(p);
	return EFI_SUCCESS;
}

static EFI_STATUS
stub_handle_protocol(EFI_HANDLE h, EFI_GUID *guid, void **iface)
{
	(void)guid;
	(void)h;
	*iface = g_vol;
	return EFI_SUCCESS;
}

static EFI_BOOT_SERVICES g_bs = {
	.AllocatePool   = stub_allocate_pool,
	.FreePool       = stub_free_pool,
	.HandleProtocol = stub_handle_protocol,
};

static EFI_SYSTEM_TABLE g_st = {
	.BootServices = &g_bs,
};

/*
 * Pull in the implementation. chain.c's build_file_device_path() is static, and
 * including the TU is the only way to reach it without changing the source for
 * the test's benefit.
 */
#include "../src/chain.c"

/* ------------------------------------------------------------------ checks */

static UINTN
wnchars(const CHAR16 *s)
{
	UINTN n = 0;

	while (s[n] != 0) {
		n++;
	}
	return n;
}

static void
check_path(const char *label, const CHAR16 *name)
{
	EFI_DEVICE_PATH_PROTOCOL *dp =
	    build_file_device_path(&g_st, (EFI_HANDLE)1, name);

	CHECK(dp != NULL, "%s: builder returned NULL", label);
	if (dp == NULL) {
		return;
	}

	UINTN vol_prefix = g_vol_len - 4;
	CHECK(memcmp(dp, g_vol, vol_prefix) == 0,
	    "%s: volume prefix was not preserved byte for byte", label);

	UINT8 *p = (UINT8 *)dp;
	UINTN  total = 0;
	int    nodes = 0;
	int    saw_end = 0;
	int    saw_filepath = 0;

	for (;;) {
		EFI_DEVICE_PATH_PROTOCOL *n = (EFI_DEVICE_PATH_PROTOCOL *)p;
		UINTN len = (UINTN)n->Length[0] | ((UINTN)n->Length[1] << 8);

		CHECK(len >= 4, "%s: node %d length %llu is below the 4-byte header",
		    label, nodes, (unsigned long long)len);
		if (len < 4) {
			break;
		}
		if (n->Type == DEVICE_PATH_TYPE_MEDIA &&
		    n->SubType == MEDIA_SUBTYPE_FILEPATH) {
			FILEPATH_DEVICE_PATH *fp = (FILEPATH_DEVICE_PATH *)n;
			UINTN chars = wnchars(name) + 1;   /* include the NUL */

			saw_filepath = 1;
			CHECK(len == 4 + chars * sizeof(CHAR16),
			    "%s: FILEPATH node is %llu bytes, expected %llu",
			    label, (unsigned long long)len,
			    (unsigned long long)(4 + chars * sizeof(CHAR16)));
			CHECK(fp->PathName[chars - 1] == 0,
			    "%s: FILEPATH name is not NUL-terminated at the right place",
			    label);
			for (UINTN i = 0; i + 1 < chars; i++) {
				if (fp->PathName[i] != name[i]) {
					CHECK(0, "%s: name differs at char %llu",
					    label, (unsigned long long)i);
					break;
				}
			}
		}
		p += len;
		total += len;
		nodes++;
		if (n->Type == DEVICE_PATH_TYPE_END &&
		    n->SubType == END_SUBTYPE_ENTIRE) {
			saw_end = 1;
			break;
		}
		if (nodes > 16) {
			CHECK(0, "%s: path does not terminate", label);
			break;
		}
	}

	CHECK(saw_end, "%s: no END/END_ENTIRE node", label);
	CHECK(saw_filepath, "%s: no FILEPATH node", label);
	CHECK(nodes == 3, "%s: expected 3 nodes (volume, filepath, end), got %d",
	    label, nodes);

	/* Exactly one END node: the prefix's own END must have been consumed. */
	{
		UINTN end_count = 0;
		UINT8 *q = (UINT8 *)dp;
		UINTN  off = 0;

		while (off + 4 <= total) {
			EFI_DEVICE_PATH_PROTOCOL *n = (EFI_DEVICE_PATH_PROTOCOL *)q;
			UINTN len = (UINTN)n->Length[0] | ((UINTN)n->Length[1] << 8);

			if (len < 4) {
				break;
			}
			if (n->Type == DEVICE_PATH_TYPE_END &&
			    n->SubType == END_SUBTYPE_ENTIRE) {
				end_count++;
			}
			q += len;
			off += len;
		}
		CHECK(end_count == 1, "%s: %llu END nodes, expected exactly 1",
		    label, (unsigned long long)end_count);
	}

	/*
	 * The buffer must be exactly as long as the nodes claim. If the builder
	 * under-allocated, this walk would already have read past the end; if it
	 * over-allocated that is harmless, so only the under case is asserted by
	 * checking the last node ends the buffer.
	 */
	CHECK(total == vol_prefix + 4 + (wnchars(name) + 1) * sizeof(CHAR16) + 4,
	    "%s: total %llu does not match the expected node sizes",
	    label, (unsigned long long)total);

	free(dp);
}

int
main(void)
{
	make_volume_path();

	printf("chainloader device path\n");

	check_path("short", L"a.efi");
	check_path("sweet\\shim.efi", L"sweet\\shim.efi");
	check_path("root image", L"BOOTAA64.EFI");
	check_path("long", L"sweet\\a-very-long-image-name-that-still-fits.efi");
	check_path("minimal", L"x.efi");

	/* The scanner stores names up to 63 characters; the path must still be sane. */
	{
		CHAR16 big[64];

		for (int i = 0; i < 59; i++) {
			big[i] = (CHAR16)('a' + (i % 26));
		}
		big[59] = '.';
		big[60] = 'e';
		big[61] = 'f';
		big[62] = 'i';
		big[63] = 0;
		check_path("63-char name", big);
	}

	printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
	if (g_failures == 0) {
		printf("PASS\n");
	}
	return g_failures == 0 ? 0 : 1;
}
