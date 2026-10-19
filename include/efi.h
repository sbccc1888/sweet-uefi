/*
 * efi.h -- the small, self-contained subset of UEFI this application needs.
 *
 * Deliberately not edk2 and not gnu-efi: this is a single freestanding EFI
 * application, so pulling in a build system for it would dwarf the code. The
 * declarations below are written from the UEFI 2.10 specification, chapter 4
 * (types) and chapter 12 (Boot Services), limited to what is actually called.
 *
 * Calling convention: aarch64 UEFI uses the standard AAPCS64, so no attribute
 * is needed. On x86_64 it would be ms_abi -- if that is ever wanted, add it
 * here in one place.
 */
#ifndef SWEET_UEFI_EFI_H
#define SWEET_UEFI_EFI_H

typedef unsigned char      UINT8;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef signed char        INT8;
typedef short              INT16;
typedef int                INT32;
typedef long long          INT64;
typedef UINT8              BOOLEAN;
typedef UINT16             CHAR16;
typedef UINT64             UINTN;
typedef INT64              INTN;
typedef UINTN              EFI_STATUS;
typedef void              *EFI_HANDLE;
typedef void              *EFI_EVENT;
typedef UINT64             EFI_PHYSICAL_ADDRESS;
typedef UINT64             EFI_VIRTUAL_ADDRESS;
typedef UINTN              EFI_TPL;

#define EFIAPI
#define IN
#define OUT
#define OPTIONAL
#define CONST const

#define EFI_TRUE  1
#define EFI_FALSE 0

/* ---- status codes (spec appendix D) ---- */
#define EFI_SUCCESS            0ULL
#define EFI_LOAD_ERROR         0x8000000000000001ULL
#define EFI_INVALID_PARAMETER  0x8000000000000002ULL
#define EFI_UNSUPPORTED        0x8000000000000003ULL
#define EFI_BAD_BUFFER_SIZE    0x8000000000000004ULL
#define EFI_BUFFER_TOO_SMALL   0x8000000000000005ULL
#define EFI_NOT_READY          0x8000000000000006ULL
#define EFI_DEVICE_ERROR       0x8000000000000007ULL
#define EFI_WRITE_PROTECTED    0x8000000000000008ULL
#define EFI_OUT_OF_RESOURCES   0x8000000000000009ULL
#define EFI_NOT_FOUND          0x800000000000000EULL
#define EFI_ABORTED            0x8000000000000015ULL

#define EFI_ERROR(s) (((INT64)(s)) < 0)

/* ---- GUIDs ---- */
typedef struct {
	UINT32 Data1;
	UINT16 Data2;
	UINT16 Data3;
	UINT8  Data4[8];
} EFI_GUID;


/* ---- text output (used only for the last-resort error path) ---- */
typedef struct {
	UINT16 ScanCode;
	CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
	void *Reset;
	EFI_STATUS (EFIAPI *OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
	void *TestString;
	void *QueryMode;
	void *SetMode;
	void *SetAttribute;
	void *ClearScreen;
	void *SetCursorPosition;
	void *EnableCursor;
	void *Mode;
};

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
	void *Reset;
	EFI_STATUS (EFIAPI *ReadKeyStroke)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *,
	    EFI_INPUT_KEY *);
	EFI_EVENT WaitForKey;
};

/* ---- time (for the frame pacing and the animation clock) ---- */
typedef struct {
	UINT16 Year, Month, Day, Hour, Minute, Second, Pad1;
	UINT32 Nanosecond;
	INT16  TimeZone;
	UINT8  Daylight;
	UINT8  Pad2;
} EFI_TIME;

typedef struct {
	UINT32 Resolution;
	UINT32 Accuracy;
	UINT8  SetsToZero;
} EFI_TIME_CAPABILITIES;

/* ---- graphics output (spec ch. 12.9) ---- */
typedef struct {
	UINT32 RedMask, GreenMask, BlueMask, ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
	PixelRedGreenBlueReserved8BitPerColor,
	PixelBlueGreenRedReserved8BitPerColor,
	PixelBitMask,
	PixelBltOnly,
	PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
	UINT32 Version;
	UINT32 HorizontalResolution;
	UINT32 VerticalResolution;
	UINT32 PixelFormat;
	EFI_PIXEL_BITMASK PixelInformation;
	UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
	UINT32 MaxMode;
	UINT32 Mode;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
	UINTN SizeOfInfo;
	EFI_PHYSICAL_ADDRESS FrameBufferBase;
	UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
	EFI_STATUS (EFIAPI *QueryMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32,
	    UINTN *, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
	EFI_STATUS (EFIAPI *SetMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32);
	EFI_STATUS (EFIAPI *Blt)(EFI_GRAPHICS_OUTPUT_PROTOCOL *, void *,
	    void *, UINT32, UINTN, UINTN, UINTN, UINTN, UINTN, UINTN, UINTN);
	EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

/* ---- block I/O, for reading boot.img off the ESP/partition ---- */
typedef struct {
	UINT32 MediaId;
	UINT8  RemovableMedia;
	UINT8  MediaPresent;
	UINT8  LogicalPartition;
	UINT8  ReadOnly;
	UINT8  WriteCaching;
	UINT32 BlockSize;
	UINT32 IoAlign;
	UINT64 LastBlock;
} EFI_BLOCK_IO_MEDIA;

typedef struct EFI_BLOCK_IO_PROTOCOL EFI_BLOCK_IO_PROTOCOL;
struct EFI_BLOCK_IO_PROTOCOL {
	UINT64 Revision;
	EFI_BLOCK_IO_MEDIA *Media;
	EFI_STATUS (EFIAPI *Reset)(EFI_BLOCK_IO_PROTOCOL *, BOOLEAN);
	EFI_STATUS (EFIAPI *ReadBlocks)(EFI_BLOCK_IO_PROTOCOL *, UINT32,
	    UINT64, UINTN, void *);
	EFI_STATUS (EFIAPI *WriteBlocks)(EFI_BLOCK_IO_PROTOCOL *, UINT32,
	    UINT64, UINTN, void *);
	EFI_STATUS (EFIAPI *FlushBlocks)(EFI_BLOCK_IO_PROTOCOL *);
};

/* ---- minimal file protocol, enough to load a payload file ---- */
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL {
	UINT64 Revision;
	EFI_STATUS (EFIAPI *Open)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **,
	    CHAR16 *, UINT64, UINT64);
	EFI_STATUS (EFIAPI *Close)(EFI_FILE_PROTOCOL *);
	EFI_STATUS (EFIAPI *Delete)(EFI_FILE_PROTOCOL *);
	EFI_STATUS (EFIAPI *Read)(EFI_FILE_PROTOCOL *, UINTN *, void *);
	EFI_STATUS (EFIAPI *Write)(EFI_FILE_PROTOCOL *, UINTN *, void *);
	EFI_STATUS (EFIAPI *GetPosition)(EFI_FILE_PROTOCOL *, UINT64 *);
	EFI_STATUS (EFIAPI *SetPosition)(EFI_FILE_PROTOCOL *, UINT64);
	EFI_STATUS (EFIAPI *GetInfo)(EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN *, void *);
	EFI_STATUS (EFIAPI *SetInfo)(EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN, void *);
	EFI_STATUS (EFIAPI *Flush)(EFI_FILE_PROTOCOL *);
};

#define EFI_FILE_MODE_READ  0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE 0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL

typedef struct {
	UINT16 Year, Month, Day, Hour, Minute, Second, Pad1;
	UINT32 Nanosecond;
	INT16  TimeZone;
	UINT8  Daylight;
	UINT8  Pad2;
	UINT64 Size;
	UINT64 FileSize;
	UINT64 PhysicalSize;
	EFI_TIME CreateTime, LastAccessTime, ModificationTime;
	UINT64 Attribute;
	CHAR16 FileName[1];
} EFI_FILE_INFO;

typedef struct {
	EFI_HANDLE AgentHandle;
	EFI_HANDLE ControllerHandle;
	UINT32 Attributes;
	UINT32 OpenCount;
} EFI_OPEN_PROTOCOL_INFORMATION_ENTRY;

typedef struct EFI_LOADED_IMAGE_PROTOCOL EFI_LOADED_IMAGE_PROTOCOL;
struct EFI_LOADED_IMAGE_PROTOCOL {
	UINT32 Revision;
	EFI_HANDLE ParentHandle;
	void *SystemTable;
	EFI_HANDLE DeviceHandle;
	void *FilePath;
	void *Reserved;
	UINT32 LoadOptionsSize;
	void *LoadOptions;
	void *ImageBase;
	UINT64 ImageSize;
	UINT32 ImageCodeType;
	UINT32 ImageDataType;
	EFI_STATUS (EFIAPI *Unload)(EFI_HANDLE);
};

/* ---- table header (spec 4.2): UINT64, UINT32, UINT32, UINT32, UINT32 ---- */
typedef struct {
	UINT64 Signature;
	UINT32 Revision;
	UINT32 HeaderSize;
	UINT32 CRC32;
	UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct EFI_RUNTIME_SERVICES EFI_RUNTIME_SERVICES;

typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;
struct EFI_SYSTEM_TABLE {
	EFI_TABLE_HEADER Hdr;
	CHAR16 *FirmwareVendor;
	UINT32  FirmwareRevision;
	EFI_HANDLE ConsoleInHandle;
	EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *ConIn;
	EFI_HANDLE ConsoleOutHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
	EFI_HANDLE StandardErrorHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
	EFI_RUNTIME_SERVICES *RuntimeServices;
	EFI_BOOT_SERVICES    *BootServices;
};

/* Memory types (spec 7.2); only the one this application allocates from. */
#define EFI_LOADER_DATA 2

/*
 * Boot Services (spec 4.4), every field in specification order. Ordering is the
 * ABI: one wrong or missing entry silently shifts every pointer after it, so
 * this is transcribed rather than abbreviated, and tools/check_layout.py
 * verifies the offsets against the spec sizes.
 */
struct EFI_BOOT_SERVICES {
	EFI_TABLE_HEADER Hdr;
	EFI_TPL  (EFIAPI *RaiseTPL)(EFI_TPL);
	void     (EFIAPI *RestoreTPL)(EFI_TPL);
	EFI_STATUS (EFIAPI *AllocatePages)(UINT32, UINT32, UINTN, EFI_PHYSICAL_ADDRESS *);
	EFI_STATUS (EFIAPI *FreePages)(EFI_PHYSICAL_ADDRESS, UINTN);
	EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN *, void *, UINTN *, UINTN *, UINT32 *);
	EFI_STATUS (EFIAPI *AllocatePool)(UINT32, UINTN, void **);
	EFI_STATUS (EFIAPI *FreePool)(void *);
	EFI_STATUS (EFIAPI *CreateEvent)(UINT32, EFI_TPL, void *, void *, EFI_EVENT *);
	EFI_STATUS (EFIAPI *SetTimer)(EFI_EVENT, UINT32, UINT64);
	EFI_STATUS (EFIAPI *WaitForEvent)(UINTN, EFI_EVENT *, UINTN *);
	EFI_STATUS (EFIAPI *SignalEvent)(EFI_EVENT);
	EFI_STATUS (EFIAPI *CloseEvent)(EFI_EVENT);
	EFI_STATUS (EFIAPI *CheckEvent)(EFI_EVENT);
	EFI_STATUS (EFIAPI *InstallProtocolInterface)(EFI_HANDLE *, EFI_GUID *, UINT32, void *);
	EFI_STATUS (EFIAPI *ReinstallProtocolInterface)(EFI_HANDLE, EFI_GUID *, void *, void *);
	EFI_STATUS (EFIAPI *UninstallProtocolInterface)(EFI_HANDLE, EFI_GUID *, void *);
	EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE, EFI_GUID *, void **);
	void        *Reserved;
	EFI_STATUS (EFIAPI *RegisterProtocolNotify)(EFI_GUID *, EFI_EVENT, void **);
	EFI_STATUS (EFIAPI *LocateHandle)(UINT32, EFI_GUID *, void *, UINTN *, EFI_HANDLE *);
	EFI_STATUS (EFIAPI *LocateDevicePath)(EFI_GUID *, void **, EFI_HANDLE *);
	EFI_STATUS (EFIAPI *InstallConfigurationTable)(EFI_GUID *, void *);
	EFI_STATUS (EFIAPI *LoadImage)(BOOLEAN, EFI_HANDLE, void *, void *, UINTN, EFI_HANDLE *);
	EFI_STATUS (EFIAPI *StartImage)(EFI_HANDLE, UINTN *, CHAR16 **);
	EFI_STATUS (EFIAPI *Exit)(EFI_HANDLE, EFI_STATUS, UINTN, CHAR16 *);
	EFI_STATUS (EFIAPI *UnloadImage)(EFI_HANDLE);
	EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE, UINTN);
	EFI_STATUS (EFIAPI *GetNextMonotonicCount)(UINT64 *);
	EFI_STATUS (EFIAPI *Stall)(UINTN);
	EFI_STATUS (EFIAPI *SetWatchdogTimer)(UINTN, UINT64, UINTN, CHAR16 *);
	EFI_STATUS (EFIAPI *ConnectController)(EFI_HANDLE, EFI_HANDLE *, void *, BOOLEAN);
	EFI_STATUS (EFIAPI *DisconnectController)(EFI_HANDLE, EFI_HANDLE, EFI_HANDLE);
	EFI_STATUS (EFIAPI *OpenProtocol)(EFI_HANDLE, EFI_GUID *, void **, EFI_HANDLE, EFI_HANDLE, UINT32);
	EFI_STATUS (EFIAPI *CloseProtocol)(EFI_HANDLE, EFI_GUID *, EFI_HANDLE, EFI_HANDLE);
	EFI_STATUS (EFIAPI *OpenProtocolInformation)(EFI_HANDLE, EFI_GUID *, void **, UINTN *);
	EFI_STATUS (EFIAPI *ProtocolsPerHandle)(EFI_HANDLE, EFI_GUID ***, UINTN *);
	EFI_STATUS (EFIAPI *LocateHandleBuffer)(UINT32, EFI_GUID *, void *, UINTN *, EFI_HANDLE **);
	EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID *, void *, void **);
	EFI_STATUS (EFIAPI *InstallMultipleProtocolInterfaces)(EFI_HANDLE *, ...);
	EFI_STATUS (EFIAPI *UninstallMultipleProtocolInterfaces)(EFI_HANDLE, ...);
	EFI_STATUS (EFIAPI *CalculateCrc32)(void *, UINTN, UINT32 *);
	void        (EFIAPI *CopyMem)(void *, CONST void *, UINTN);
	void        (EFIAPI *SetMem)(void *, UINTN, UINT8);
};

struct EFI_RUNTIME_SERVICES {
	char opaque[1];   /* not used by this application */
};

/* ---- GUIDs ---- */
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
	{ 0x9042a9de, 0x23dc, 0x4a38, \
	  { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
	{ 0x5b1b31a1, 0x9562, 0x11d2, \
	  { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
	{ 0x964e5b22, 0x6459, 0x11d2, \
	  { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_FILE_INFO_ID \
	{ 0x09576e92, 0x6d3f, 0x11d2, \
	  { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
	UINT64 Revision;
	EFI_STATUS (EFIAPI *OpenVolume)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *,
	    EFI_FILE_PROTOCOL **);
};

#endif /* SWEET_UEFI_EFI_H */
