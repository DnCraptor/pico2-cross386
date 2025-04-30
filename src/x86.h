#ifndef X86_H
#define X86_H

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

#define X86_RAM_BASE 0x11000000
#define X86_FAR_PTR(S, X) (u8*)(X86_RAM_BASE + (X) + ((u32)S << 4))

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
__attribute__((naked)) void x86_int21_hanler();

// to call from gcc
uint32_t x86_int10_wrapper(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) __attribute__((pcs("aapcs")));
uint32_t x86_int13_wrapper(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) __attribute__((pcs("aapcs")));
uint32_t x86_int16_wrapper(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) __attribute__((pcs("aapcs")));
uint32_t x86_int16_wrapper(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) __attribute__((pcs("aapcs")));
uint32_t x86_raise_interrupt_wrapper(uint32_t eax) __attribute__((pcs("aapcs")));

// x86 BIOS implementation
u32 x86_int10_hanler_C(u32 eax, u32 ebx, u32 ecx, u32 edx) __attribute__((pcs("aapcs")));
u32 x86_int13_hanler_C(u32 eax, u32 ebx, u32 ecx, u32 edx) __attribute__((pcs("aapcs")));

void x86_add_char_to_BDA(u16 keycode);


#define KF1_LAST_E1    (1<<0)
#define KF1_LAST_E0    (1<<1)
#define KF1_RCTRL      (1<<2)
#define KF1_RALT       (1<<3)
#define KF1_101KBD     (1<<4)
struct __attribute__((packed)) segoff_s {
    union {
        struct {
            u16 offset;
            u16 seg;
        };
        u32 segoff;
    };
};

struct __attribute__((packed)) bios_data_area_s {
    // 40:00
    u16 port_com[4];
    u16 port_lpt[3];
    u16 ebda_seg;
    // 40:10
    u16 equipment_list_flags;
    u8 pad1;
    u16 mem_size_kb;
    u8 pad2;
    u8 ps2_ctrl_flag;
    u16 kbd_flag0;
    u8 alt_keypad;
    u16 kbd_buf_head;
    u16 kbd_buf_tail;
    // 40:1e
    u8 kbd_buf[32];
    u8 floppy_recalibration_status;
    u8 floppy_motor_status;
    // 40:40
    u8 floppy_motor_counter;
    u8 floppy_last_status;
    u8 floppy_return_status[7];
    u8 video_mode;
    u16 video_cols;
    u16 video_pagesize;
    u16 video_pagestart;
    // 40:50
    u16 cursor_pos[8];
    // 40:60
    u16 cursor_type;
    u8 video_page;
    u16 crtc_address;
    u8 video_msr;
    u8 video_pal;
    struct segoff_s jump;
    u8 other_6b;
    u32 timer_counter;
    // 40:70
    u8 timer_rollover;
    u8 break_flag;
    u16 soft_reset_flag;
    u8 disk_last_status;
    u8 hdcount;
    u8 disk_control_byte;
    u8 port_disk;
    u8 lpt_timeout[4];
    u8 com_timeout[4];
    // 40:80
    u16 kbd_buf_start_offset;
    u16 kbd_buf_end_offset;
    u8 video_rows;
    u16 char_height;
    u8 video_ctl;
    u8 video_switches;
    u8 modeset_ctl;
    u8 dcc_index;
    u8 floppy_last_data_rate;
    u8 disk_status_controller;
    u8 disk_error_controller;
    u8 disk_interrupt_flag;
    u8 floppy_harddisk_info;
    // 40:90
    u8 floppy_media_state[4];
    u8 floppy_track[2];
    u8 kbd_flag1;
    u8 kbd_led;
    struct segoff_s user_wait_complete_flag;
    u32 user_wait_timeout;
    // 40:A0
    u8 rtc_wait_flag;
    u8 other_a1[7];
    struct segoff_s video_savetable;
    u8 other_ac[4];
    // 40:B0
    u8 other_b0[5*16];
};

#ifdef __cplusplus
}
#endif

#endif // X86_H
