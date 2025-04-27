#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void x86_init(void);

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define AL ((u8)(eax))
#define AH ((u8)((eax >> 8)))
#define AX ((u16)(eax))
#define BL ((u8)(ebx))
#define BH ((u8)((ebx >> 8)))
#define BX ((u16)(ebx))
#define CL ((u8)(ecx))
#define CH ((u8)((ecx >> 8)))
#define CX ((u16)(ecx))
#define DL ((u8)(edx))
#define DH ((u8)((edx >> 8)))
#define DX ((u16)(edx))
#define DI ((u16)(edi))

#define CF_ON (1l << 29)
#define ZF_ON (1l << 30)

extern u16 X86_CS;
extern u16 X86_DS;
extern u16 X86_ES;
extern u16 X86_FS;
extern u16 X86_GS;
extern u16 X86_SS;
extern u32 X86_CR0;

static const u32 X86_RAM_BASE = 0x11000000;

inline static u8* X86_FAR_PTR(uint16_t S, uint16_t X) { return (u8*)(X86_RAM_BASE + X + ((u32)S << 4)); }

#define CONFIG_CDROM_EMU 0

extern bool cd_card_mount;
extern const char HOME_DIR[];

typedef struct chs_s {
    u16 head;
    u16 cylinder;
    u16 sector;
    u16 pad;
} chs_t;

typedef struct FIL_s FIL;
typedef struct drive_s {
    u8 type;            // Driver type (DTYPE_*)
    u8 floppy_type;     // Type of floppy (only for floppy drives).
    chs_t lchs;         // Logical CHS
    u64 sectors;        // Total sectors count
    u32 cntl_id;        // Unique id for a given driver type.
    u8 removable;       // Is media removable (currently unused)

    // Info for EDD calls
    u8 translation;     // type of translation
    u16 blksize;        // block size
    chs_t pchs;         // Physical CHS
    u32 max_segment_size; //max_segment_size
    u32 max_segments;   //max_segments
    // file pre-open for this drive
    FIL* pf;
} drive_t;

#define DISK_SECTOR_SIZE  512
#define CDROM_SECTOR_SIZE 2048

#define DTYPE_NONE         0x00
#define DTYPE_FLOPPY       0x10
#define DTYPE_ATA          0x20
#define DTYPE_ATA_ATAPI    0x21
#define DTYPE_RAMDISK      0x30
#define DTYPE_CDEMU        0x40
#define DTYPE_AHCI         0x50
#define DTYPE_AHCI_ATAPI   0x51
#define DTYPE_VIRTIO_SCSI  0x60
#define DTYPE_VIRTIO_BLK   0x61
#define DTYPE_USB          0x70
#define DTYPE_USB_32       0x71
#define DTYPE_UAS          0x72
#define DTYPE_UAS_32       0x73
#define DTYPE_LSI_SCSI     0x80
#define DTYPE_ESP_SCSI     0x81
#define DTYPE_MEGASAS      0x82
#define DTYPE_PVSCSI       0x83
#define DTYPE_MPT_SCSI     0x84
#define DTYPE_SDCARD       0x90
#define DTYPE_NVME         0x91

// x86 commands implementation
__attribute__((naked)) void x86_iret(void);

// x86 BIOS asm wrappers
__attribute__((naked)) void x86_int10_hanler();
__attribute__((naked)) void x86_int11_hanler();
__attribute__((naked)) void x86_int12_hanler();
__attribute__((naked)) void x86_int13_hanler();
__attribute__((naked)) void x86_int15_hanler();
__attribute__((naked)) void x86_int16_hanler();

// x86 BIOS implementation
u32 x86_int10_hanler_C(u32 eax, u32 ebx, u32 ecx, u32 edx) __attribute__((pcs("aapcs")));
u32 x86_int13_hanler_C(u32 eax, u32 ebx, u32 ecx, u32 edx) __attribute__((pcs("aapcs")));

void x86_add_char_to_BDA(u8 scan, u8 ascii);
void x86_update_kbd_BDA(u8 keyboard_status, u8 extended_status);

#ifdef __cplusplus
}
#endif
