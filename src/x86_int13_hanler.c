#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pico.h>
#include <pico/stdlib.h>
#include "ff.h"
#include "x86.h"

bool cd_card_mount = false;
const char HOME_DIR[] = "/cross";

#define DISK_RET_SUCCESS       0x00
#define DISK_RET_EPARAM        0x01 << 8
#define DISK_RET_EADDRNOTFOUND 0x02 << 8
#define DISK_RET_EWRITEPROTECT 0x03 << 8
#define DISK_RET_ECHANGED      0x06 << 8
#define DISK_RET_EBOUNDARY     0x09 << 8
#define DISK_RET_EBADTRACK     0x0c << 8
#define DISK_RET_ECONTROLLER   0x20 << 8
#define DISK_RET_NO_MEDIA      0x31 << 8
#define DISK_RET_ETIMEOUT      0x80 << 8
#define DISK_RET_ENOTLOCKED    0xb0 << 8
#define DISK_RET_ELOCKED       0xb1 << 8
#define DISK_RET_ENOTREMOVABLE 0xb2 << 8
#define DISK_RET_ETOOMANYLOCKS 0xb4 << 8
#define DISK_RET_EMEDIA        0xC0 << 8
#define DISK_RET_ENOTREADY     0xAA << 8

static u32 last_op_res = DISK_RET_SUCCESS;

typedef struct floppyinfo_s {
    chs_t chs;
    u8 floppy_size;
    u8 data_rate;
} floppyinfo_t;

#define FLOPPY_SIZE_525 0x01
#define FLOPPY_SIZE_350 0x02

#define FLOPPY_RATE_500K 0x00
#define FLOPPY_RATE_300K 0x01
#define FLOPPY_RATE_250K 0x02
#define FLOPPY_RATE_1M   0x03

const static floppyinfo_t FloppyInfo[] = {
    // Unknown
    { {0, 0, 0}, 0x00, 0x00},
    // 1 - 360KB, 5.25" - 2 heads, 40 tracks, 9 sectors
    { {2, 40, 9}, FLOPPY_SIZE_525, FLOPPY_RATE_300K},
    // 2 - 1.2MB, 5.25" - 2 heads, 80 tracks, 15 sectors
    { {2, 80, 15}, FLOPPY_SIZE_525, FLOPPY_RATE_500K},
    // 3 - 720KB, 3.5"  - 2 heads, 80 tracks, 9 sectors
    { {2, 80, 9}, FLOPPY_SIZE_350, FLOPPY_RATE_250K},
    // 4 - 1.44MB, 3.5" - 2 heads, 80 tracks, 18 sectors
    { {2, 80, 18}, FLOPPY_SIZE_350, FLOPPY_RATE_500K},
    // 5 - 2.88MB, 3.5" - 2 heads, 80 tracks, 36 sectors
    { {2, 80, 36}, FLOPPY_SIZE_350, FLOPPY_RATE_1M},
    // 6 - 160k, 5.25"  - 1 heads, 40 tracks, 8 sectors
    { {1, 40, 8}, FLOPPY_SIZE_525, FLOPPY_RATE_250K},
    // 7 - 180k, 5.25"  - 1 heads, 40 tracks, 9 sectors
    { {1, 40, 9}, FLOPPY_SIZE_525, FLOPPY_RATE_300K},
    // 8 - 320k, 5.25"  - 2 heads, 40 tracks, 8 sectors
    { {2, 40, 8}, FLOPPY_SIZE_525, FLOPPY_RATE_250K},
};

/* Feature bits */
#define VIRTIO_BLK_F_SIZE_MAX 1  /* Maximum size of any single segment */
#define VIRTIO_BLK_F_SEG_MAX 2   /* Maximum number of segments in a request */
#define VIRTIO_BLK_F_BLK_SIZE 6

static drive_t fdds[2] = {
    {
        DTYPE_FLOPPY,
        4, // 1.44
        {2, 80, 18},
        2 * 80 * 18,
        0,
        1,
        0,
        DISK_SECTOR_SIZE,
        {2, 80, 18},
    },
    {
        DTYPE_FLOPPY,
        4, // 1.44
        {2, 80, 18},
        2 * 80 * 18,
        1,
        1,
        0,
        DISK_SECTOR_SIZE,
        {2, 80, 18},
    }
};
static drive_t hdds[4] = {
    {
        DTYPE_ATA,
        0,
        {256, 512, 63},
        256 * 512 * 63,
        0,
        0,
        0,
        DISK_SECTOR_SIZE,
        {256, 512, 63},
        1ull << VIRTIO_BLK_F_SIZE_MAX,
        1ull << VIRTIO_BLK_F_SEG_MAX
    },
    {
        DTYPE_ATA,
        0,
        {256, 512, 63},
        256 * 512 * 63,
        1,
        0,
        0,
        DISK_SECTOR_SIZE,
        {256, 512, 63},
        1ull << VIRTIO_BLK_F_SIZE_MAX,
        1ull << VIRTIO_BLK_F_SEG_MAX
    },
    {
        DTYPE_ATA,
        0,
        {256, 512, 63},
        256 * 512 * 63,
        2,
        0,
        0,
        DISK_SECTOR_SIZE,
        {256, 512, 63},
        1ull << VIRTIO_BLK_F_SIZE_MAX,
        1ull << VIRTIO_BLK_F_SEG_MAX
    },
    {
        DTYPE_ATA,
        0,
        {256, 512, 63},
        256 * 512 * 63,
        3,
        0,
        0,
        DISK_SECTOR_SIZE,
        {256, 512, 63},
        1ull << VIRTIO_BLK_F_SIZE_MAX,
        1ull << VIRTIO_BLK_F_SEG_MAX
    }
};

u8 get_drive_count(u8 type) {
    /// TODO: dynamic detection
    if (type == DTYPE_ATA) return 4;
    if (type == DTYPE_FLOPPY) return 2;
    return 0;
}

static drive_t* getDrive(u8 extdrive) {
    drive_t* res = 0;
    const char* format = "%s/hdd%02X.img";
    if (extdrive < 3) {
        format = "%s/fdd%02X.img";
        res = &fdds[extdrive];
        goto r;
    }
    if (extdrive >= 0x80) {
        extdrive &= 0b01111111;
        if (extdrive < 5) {
            res = &hdds[extdrive];
            goto r;
        }
    }
    return 0;
r:
    if (!res->pf) {
        char filename[32];
        snprintf(filename, 32, format, HOME_DIR, extdrive);
        res->pf = calloc(sizeof(FIL), 1);
        if (f_open(res->pf, filename, FA_READ | FA_WRITE | FA_OPEN_ALWAYS) != FR_OK) {
            free(res->pf);
            res->pf = 0;
            return 0;
        }
    }
    return res;
}

/**
 * DISK - RESET DISK SYSTEM
AH = 00h
DL = drive (if bit 7 is set both hard disks and floppy disks reset)

Return:
AH = status (see #00234)
CF clear if successful (returned AH=00h)
CF set on error
 */

/**
 * DISK - READ SECTOR(S) INTO MEMORY
 * AH = 02h
 * AL = number of sectors to read (must be nonzero)
 * CH = low eight bits of cylinder number
 * CL = sector number 1-63 (bits 0-5)
 * high two bits of cylinder (bits 6-7, hard disk only)
 * DH = head number
 * DL = drive number (bit 7 set for hard disk)
 * ES:BX -> data buffer
 * 
 * Returns:
 * CF set on error
 * if AH = 11h (corrected ECC error), AL = burst length
 * CF clear if successful
 * AH = status (see #00234)
 * AL = number of sectors transferred (only valid if CF set for some BIOSes)
 */
inline static u32 handle_int13_02(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    u8 number_of_sectors_to_read = AL;
    if (!number_of_sectors_to_read) return DISK_RET_EPARAM | CF_ON;

    drive_t *drive = getDrive(DL);
    if (!drive) return DISK_RET_NO_MEDIA | CF_ON;

    u32 cylinder_number = ((CL & 0xC0) << 2) | CH;
    u32 sector_number = CL & 0x3F;
    u8 head_number = DH;

    if (sector_number == 0 || head_number > drive->lchs.head || cylinder_number > drive->lchs.cylinder)
        return DISK_RET_EPARAM | CF_ON;

    u32 lba_sector = ((cylinder_number * (drive->lchs.head + 1)) + head_number) * drive->lchs.sector + (sector_number - 1);
    if ((u64)lba_sector + number_of_sectors_to_read > drive->sectors)
        return DISK_RET_EBOUNDARY | CF_ON;

    gpio_put(PICO_DEFAULT_LED_PIN, true);
    FSIZE_t offset = (FSIZE_t)lba_sector * DISK_SECTOR_SIZE;
    if (f_lseek(drive->pf, offset) != FR_OK) {
        gpio_put(PICO_DEFAULT_LED_PIN, false);
        return DISK_RET_ECONTROLLER | CF_ON;
    }

    u8* buff = X86_FAR_PTR(X86_ES, BX);
    UINT br;
    if (f_read(drive->pf, buff, number_of_sectors_to_read * DISK_SECTOR_SIZE, &br) != FR_OK) {
        gpio_put(PICO_DEFAULT_LED_PIN, false);
        return DISK_RET_ECONTROLLER | CF_ON;
    }

    gpio_put(PICO_DEFAULT_LED_PIN, false);
    return br / DISK_SECTOR_SIZE;
}

/**
 * DISK - WRITE DISK SECTOR(S)
 * AH = 03h
 * AL = number of sectors to write (must be nonzero)
 * CH = low eight bits of cylinder number
 * CL = sector number 1-63 (bits 0-5)
 * high two bits of cylinder (bits 6-7, hard disk only)
 * DH = head number
 * DL = drive number (bit 7 set for hard disk)
 * ES:BX -> data buffer
 * 
 * Returns:
 * CF set on error
 * CF clear if successful
 * AH = status (see #00234)
 * AL = number of sectors transferred
 * (only valid if CF set for some BIOSes)
 */
inline static u32 handle_int13_03(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    u8 number_of_sectors_to_write = AL;
    if (!number_of_sectors_to_write) return DISK_RET_EPARAM | CF_ON;

    drive_t *drive = getDrive(DL);
    if (!drive) return DISK_RET_NO_MEDIA | CF_ON;

    // Проверка на защищенность от записи
//    if (drive->readonly) return DISK_RET_EWRITEPROTECT;

    u8* buff = X86_FAR_PTR(X86_ES, BX);
    u32 sector_number = CL & 0b00111111;  // [1..63]
    u32 cylinder_number = ((CL & 0xC0) << 2) | CH;  // 10 bits
    u8 head_number = DH;

    if (sector_number == 0 || head_number > drive->lchs.head || cylinder_number > drive->lchs.cylinder) {
        return DISK_RET_EPARAM | CF_ON;
    }

    FSIZE_t lba = ((u32)cylinder_number * (drive->lchs.head + 1) + head_number) * drive->lchs.sector + (sector_number - 1);

    // Проверка на выход за пределы
    if ((lba + number_of_sectors_to_write) > drive->sectors) {
        return DISK_RET_EBOUNDARY | CF_ON;
    }
    lba *= DISK_SECTOR_SIZE;

    gpio_put(PICO_DEFAULT_LED_PIN, true);
    if (f_lseek(drive->pf, lba) != FR_OK) {
        gpio_put(PICO_DEFAULT_LED_PIN, false);
        return DISK_RET_EPARAM | CF_ON;
    }

    UINT br = 0;
    if (f_write(drive->pf, buff, number_of_sectors_to_write * DISK_SECTOR_SIZE, &br) != FR_OK) {
        gpio_put(PICO_DEFAULT_LED_PIN, false);
        return DISK_RET_EPARAM | CF_ON;
    }

    f_sync(drive->pf);
    gpio_put(PICO_DEFAULT_LED_PIN, false);
    return br / DISK_SECTOR_SIZE;
}

/**
 * FLOPPY - FORMAT TRACK
 * AH = 05h
 * AL = number of sectors to format
 * CH = track number
 * DH = head number
 * DL = drive number
 * ES:BX -> address field buffer (see #00235)
 * 
 * Returns:
 * CF set on error
 * CF clear if successful
 * AH = status (see #00234)
 * 
 * Offset  Size    Description     (Table 00235)
 * 00h    BYTE    track number
 * 01h    BYTE    head number (0-based)
 * 02h    BYTE    sector number
 * 03h    BYTE    sector size (00h=128 bytes, 01h=256 bytes, 02h=512, 03h=1024)
 */
inline static u32 handle_int13_05_fdd(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    drive_t *drive = getDrive(DL);
    if (!drive) {
        return DISK_RET_NO_MEDIA;
    }
    if (drive->type != DTYPE_FLOPPY) {
        return DISK_RET_EPARAM;
    }

    u8 buf[DISK_SECTOR_SIZE] = { 0 };
    gpio_put(PICO_DEFAULT_LED_PIN, true);
    u8* buff = X86_FAR_PTR(X86_ES, BX);
    for (int sectorN = 0; sectorN < AL; ++sectorN) {
        u8 track_num = buff[sectorN * 4 + 0];
        u8 head_num  = buff[sectorN * 4 + 1];
        u8 sector_num = buff[sectorN * 4 + 2];
        u8 sector_size_code = buff[sectorN * 4 + 3];

        if (sector_size_code != 2) { // только 512 байт поддерживаем
            gpio_put(PICO_DEFAULT_LED_PIN, false);
            return DISK_RET_EPARAM;
        }

        // Теперь считаем LBA:
        // LBA = (track_number * heads_per_cylinder + head_number) * sectors_per_track + (sector_number - 1)
        u32 heads_per_cylinder = drive->lchs.head;      // число головок
        u32 sectors_per_track  = drive->lchs.sector;    // Число секторов на дорожку

        if (head_num >= heads_per_cylinder || sector_num == 0 || sector_num > sectors_per_track) {
            gpio_put(PICO_DEFAULT_LED_PIN, false);
            return DISK_RET_EPARAM;
        }

        FSIZE_t lba = ((FSIZE_t)track_num * heads_per_cylinder + head_num) * sectors_per_track + (sector_num - 1);
        lba *= DISK_SECTOR_SIZE; // в байтах
        if (f_lseek(drive->pf, lba) != FR_OK) {
            gpio_put(PICO_DEFAULT_LED_PIN, false);
            return DISK_RET_EPARAM;
        }
        UINT br;
        if (f_write(drive->pf, buf, DISK_SECTOR_SIZE, &br) != FR_OK || br != DISK_SECTOR_SIZE) {
            gpio_put(PICO_DEFAULT_LED_PIN, false);
            return DISK_RET_EPARAM;
        }
    }
    f_sync(drive->pf);
    gpio_put(PICO_DEFAULT_LED_PIN, false);
    return DISK_RET_SUCCESS;
}

/**
 * FIXED DISK - FORMAT TRACK
 * AH = 05h
 * AL = interleave value (XT-type controllers only)
 * ES:BX -> 512-byte format buffer
 * the first 2*(sectors/track) bytes contain F,N for each sector
 * F = sector type
 * 00h for good sector
 * 20h to unassign from alternate location
 * 40h to assign to alternate location
 * 80h for bad sector
 * N = sector number
 * CH = cylinder number (bits 8,9 in high bits of CL)
 * CL = high bits of cylinder number (bits 7,6)
 * DH = head
 * DL = drive
 * 
 * Returns:
 * CF set on error
 * CF clear if successful
 * AH = status code (see #00234)
 */
inline static u32 handle_int13_05_hdd(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    drive_t *drive = getDrive(DL);
    if (!drive) return DISK_RET_NO_MEDIA;
    if (drive->type == DTYPE_FLOPPY) return DISK_RET_EPARAM;

    gpio_put(PICO_DEFAULT_LED_PIN, true);

    u8* fmtbuf = X86_FAR_PTR(X86_ES, BX);

    // Извлекаем CHS
    u16 cylinder = ((CL & 0xC0) << 2) | CH;  // CL[7:6] → bits 8-9, CH → bits 0-7
    u8 head = DH;
    u32 sectors_per_track = drive->pchs.sector;

    u8 buf[DISK_SECTOR_SIZE] = {0};
    // Ожидаем, что первые 2*(spt) байта содержат F и N пары
    for (u32 i = 0; i < sectors_per_track; ++i) {
        u8 flag = fmtbuf[2 * i];
        u8 sect_num = fmtbuf[2 * i + 1];

        // Пропускаем "непривязанный" или "альтернативный" сектор
        if (flag & 0x60) continue;

        // Сектор плохой — в эмуляции мы его игнорируем
        if (flag & 0x80) continue;

        // Вычисляем LBA
        FSIZE_t lba = ((cylinder * drive->pchs.head) + head) * sectors_per_track + (sect_num - 1);
        lba *= DISK_SECTOR_SIZE;
        if (f_lseek(drive->pf, lba) != FR_OK) {
            gpio_put(PICO_DEFAULT_LED_PIN, false);
            return DISK_RET_EPARAM;
        }

        UINT bw;
        if (f_write(drive->pf, buf, DISK_SECTOR_SIZE, &bw) != FR_OK || bw != DISK_SECTOR_SIZE) {
            gpio_put(PICO_DEFAULT_LED_PIN, false);
            return DISK_RET_EPARAM;
        }
    }
    f_sync(drive->pf);
    gpio_put(PICO_DEFAULT_LED_PIN, false);
    return DISK_RET_SUCCESS;
}

/**
 * FIXED DISK - FORMAT TRACK AND SET BAD SECTOR FLAGS (XT,PORT)
 * 
 * AH = 06h
 * AL = interleave value
 * CH = cylinder number (bits 8,9 in high bits of CL)
 * CL = sector number
 * DH = head
 * DL = drive
 * 
 * Returns:
 * AH = status code (see #00234)
 * Note: AWARD AT BIOS and AMI 386sx BIOS have been extended to handle more than 1024 cylinders by placing bits 10 and 11 of the cylinder number into bits 6 and 7 of DH
 */
inline static u32 handle_int13_06(u32 eax, u32 ecx, u32 edx) {
    drive_t *drive = getDrive(DL);
    if (!drive) return DISK_RET_NO_MEDIA;

    // Извлекаем расширенный CHS (поддержка >1024 цилиндров по расширению AMI/AWARD BIOS)
    u16 cylinder = ((CL & 0xC0) << 2) | CH;
    cylinder |= (DH & 0xC0) << 4;  // DH[6:7] → cylinder[10:11]
    u8 head = DH & 0x3F;
    u8 sector_count = CL & 0x3F;

    u32 sectors_per_track = drive->pchs.sector;
    if (sector_count == 0 || sector_count > sectors_per_track)
        return DISK_RET_EPARAM;

    UINT bw;
    u8 buf[DISK_SECTOR_SIZE] = {0};

    for (u8 i = 0; i < sector_count; ++i) {
        FSIZE_t lba = ((cylinder * drive->pchs.head + head) * sectors_per_track) + i;
        lba *= DISK_SECTOR_SIZE;
        if (f_lseek(drive->pf, lba) != FR_OK) {
            return DISK_RET_EPARAM;
        }
        if (f_write(drive->pf, buf, DISK_SECTOR_SIZE, &bw) != FR_OK || bw != DISK_SECTOR_SIZE) {
            return DISK_RET_EPARAM;
        }
    }
    f_sync(drive->pf);
    return DISK_RET_SUCCESS;
}

/**
 * FIXED DISK - FORMAT DRIVE STARTING AT GIVEN TRACK (XT,PORT)
 * AH = 07h
 * AL = interleave value (XT only)
 * ES:BX = 512-byte format buffer (see AH=05h)
 * CH = cylinder number (bits 8,9 in high bits of CL)
 * CL = sector number
 * DH = head
 * DL = drive
 * 
 * Returns:
 * AH = status code (see #00234)
 * Note: AWARD AT BIOS and AMI 386sx BIOS have been extended to handle more than 1024 cylinders by placing bits 10 and 11 of the cylinder number into bits 6 and 7 of DH
 */
inline static u32 handle_int13_07(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    drive_t *drive = getDrive(DL);
    if (!drive) return DISK_RET_NO_MEDIA;

    // Расширенный CHS
    u16 cylinder = ((CL & 0xC0) << 2) | CH;
    cylinder |= (DH & 0xC0) << 4;  // DH[6:7] → cylinder[10:11]
    u8 head = DH & 0x3F;
    u8 sector_count = CL & 0x3F;

    u32 sectors_per_track = drive->pchs.sector;
    if (sector_count == 0 || sector_count > sectors_per_track)
        return DISK_RET_EPARAM;

    UINT bw;
    u8 buf[DISK_SECTOR_SIZE] = {0};

    for (u8 i = 0; i < sector_count; ++i) {
        FSIZE_t lba = ((cylinder * drive->pchs.head + head) * sectors_per_track) + i;
        lba *= DISK_SECTOR_SIZE;
        if (f_lseek(drive->pf, lba) != FR_OK) {
            return DISK_RET_EPARAM;
        }
        if (f_write(drive->pf, buf, DISK_SECTOR_SIZE, &bw) != FR_OK || bw != DISK_SECTOR_SIZE) {
            return DISK_RET_EPARAM;
        }
    }
    f_sync(drive->pf);
    return DISK_RET_SUCCESS;
}

/**
 * DISK - GET DRIVE PARAMETERS (PC,XT286,CONV,PS,ESDI,SCSI)
 * AH = 08h
 * DL = drive (bit 7 set for hard disk)
 * ES:DI = 0000h:0000h to guard against BIOS bugs
 * 
 * Returns:
 * CF set on error
 * AH = status (07h) (see #00234)
 * CF clear if successful
 * AH = 00h
 * AL = 00h on at least some BIOSes
 * BL = drive type (AT/PS2 floppies only) (see #00242)
 * CH = low eight bits of maximum cylinder number
 * CL = maximum sector number (bits 5-0)
 * high two bits of maximum cylinder number (bits 7-6)
 * DH = maximum head number
 * DL = number of drives
 * ES:DI -> drive parameter table (floppies only)
 * 
 * Notes: May return successful even though specified drive is greater than the number of attached drives of that type (floppy/hard); check DL to ensure validity. For systems predating the IBM AT, this call is only valid for hard disks, as it is implemented by the hard disk BIOS rather than the ROM BIOS. The IBM ROM-BIOS returns the total number of hard disks attached to the system regardless of whether DL >= 80h on entry.. Toshiba laptops with HardRAM return DL=02h when called with DL=80h, but fail on DL=81h. The BIOS data at 40h:75h correctly reports 01h.. May indicate only two drives present even if more are attached; to ensure a correct count, one can use AH=15h to scan through possible drives. Reportedly some Compaq BIOSes with more than one hard disk controller return only the number of drives DL attached to the corresponding controller as specified by the DL value on entry. However, on Compaq machines with "COMPAQ" signature at F000h:FFEAh, MS-DOS/PC DOS IO.SYS/IBMBIO.COM call INT 15/AX=E400h and INT 15/AX=E480h to enable Compaq "mode 2" before retrieving the count of hard disks installed in the system (DL) from this function.. The maximum cylinder number reported in CX is usually two less than the total cylinder count reported in the fixed disk parameter table (see INT 41h,INT 46h) because early hard disks used the last cylinder for testing purposes; however, on some Zenith machines, the maximum cylinder number reportedly is three less than the count in the fixed disk parameter table.. For BIOSes which reserve the last cylinder for testing purposes, the cylinder count is automatically decremented. On PS/1s with IBM ROM DOS 4, nonexistent drives return CF clear, BX=CX=0000h, and ES:DI = 0000h:0000h. Machines with lost CMOS memory may return invalid data for floppy drives. In this situation CF is cleared, but AX,BX,CX,DX,DH,DI, and ES contain only 0. At least under some circumstances, MS-DOS/ PC DOS IO.SYS/IBMBIO.COM just assumes a 360 KB floppy if it sees CH to be zero for a floppy.. The PC-Tools PCFORMAT program requires that AL=00h before it will proceed with the formatting. If this function fails, an alternative way to retrieve the number of floppy drives installed in the system is to call INT 11h.. In fact, the MS-DOS/PC-DOS IO.SYS/IBMBIO.COM attempts to get the number of floppy drives installed from INT 13/AH=08h, when INT 11h AX bit 0 indicates there are no floppy drives installed. In addition to testing the CF flag, it only trusts the result when the number of sectors (CL preset to zero) is non-zero after the call.
 * BUGS: Several different Compaq BIOSes incorrectly report high-numbered drives (such as 90h, B0h, D0h, and F0h) as present, giving them the same geometry as drive 80h; as a workaround, scan through disk numbers, stopping as soon as the number of valid drives encountered equals the value in 0040h:0075h. A bug in Leading Edge 8088 BIOS 3.10 causes the DI,SI,BP,DS, and ES registers to be destroyed. Some Toshiba BIOSes (at least before 1995, maybe some laptops??? with 1.44 MB floppies) have a bug where they do not set the ES:DI vector even for floppy drives. Hence these registers should be preset with zero before the call and checked to be non-zero on return before using them. Also it seems these BIOSes can return wrong info in BL and CX, as S/DOS 1.0 can be configured to preset these registers as for an 1.44 MB floppy.. The PS/2 Model 30 fails to reset the bus after INT 13/AH=08h and INT 13/AH=15h. A workaround is to monitor for these functions and perform a transparent INT 13/AH=01h status read afterwards. This will reset the bus. The MS-DOS 6.0 IO.SYS takes care of this by installing a special INT 13h interceptor for this purpose.. AD-DOS may leave interrupts disabled on return from this function.. Some Microsoft software explicitly sets STI after return.
 * See Also: AH=06h"Adaptec" - AH=13h"SyQuest" - AH=48h - AH
 * See Also: INT 41"HARD DISK 0"
 * (Table 00242)
 * Values for diskette drive type:
 * 01h    360K
 * 02h    1.2M
 * 03h    720K
 * 04h    1.44M
 * 05h    ??? (reportedly an obscure drive type shipped on some IBM machines).
 * 2.88M on some machines (at least AMI 486 BIOS)
 * 06h    2.88M
 * 10h    ATAPI Removable Media Device
 */
inline static u32 handle_int13_08(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    u32 edi;
    __asm volatile ("mov %0, r9" : "=r"(edi));

    drive_t *drive = getDrive(DL);
    if (!drive) return DISK_RET_NO_MEDIA;

    u8 max_heads = drive->pchs.head;
    u16 max_cylinders = drive->pchs.cylinder;
    u8 max_sectors = drive->pchs.sector;

    // CH — младшие 8 бит цилиндра
    // CL — сектора (0–5), старшие 2 бита цилиндра (6–7)
    // DH — максимум голов
    // DL — количество устройств
    // AL, AH — стандартные значения
  //  X86_AH = 0x00; DISK_RET_SUCCESS writes to eax in wrapper
  //  X86_AL = 0x00;
    u32 X86_BL = (drive->type == DTYPE_FLOPPY) ? drive->floppy_type : 0;
    u32 X86_CH = (u8)(max_cylinders & 0xFF);
    u32 X86_CL = (u8)((max_sectors & 0x3F) | ((max_cylinders >> 2) & 0xC0));
    u32 X86_DH = (u8)(max_heads & 0xFF);
    u32 X86_DL = get_drive_count(drive->type); // функция, которая возвращает количество устройств данного типа

    // ES:DI -> drive parameter table (только для флоппи)
    if (drive->type == DTYPE_FLOPPY) {
        u8 *ptr = X86_FAR_PTR(X86_ES, DI);
        memset(ptr, 0, sizeof(chs_t));  // или можно записать что-то полезное по желанию
    }

    __asm volatile ("mov r5, %0" :: "r"(X86_BL): "r5"); // R5 ← BL (x86 EBX low byte)
    __asm volatile ("mov r6, %0" :: "r"((X86_CH << 8) | X86_CL): "r6"); // R6 ← CX (CH:CL packed)
    __asm volatile ("mov r7, %0" :: "r"((X86_DH << 8) | X86_DL): "r7"); // R7 ← DX (DH:DL packed)
    return DISK_RET_SUCCESS;
}

/**
 * HARD DISK - READ LONG SECTOR(S) (AT and later)
 * AH = 0Ah
 * AL = number of sectors (01h may be only value supported)
 * CH = low eight bits of cylinder number
 * CL = sector number (bits 5-0)
 * high two bits of cylinder number (bits 7-6)
 * DH = head number
 * DL = drive number (80h = first, 81h = second)
 * ES:BX -> data buffer
 * 
 * Returns:
 * CF clear if successful
 * CF set on error
 * AH = status (see #00234)
 * AL = number of sectors transferred
 * Notes: This function reads in four to seven bytes of error-correcting code along with each sector's worth of information. Data errors are not automatically corrected, and the read is aborted after the first sector with an ECC error. Used for diagnostics only on PS/2 systems; IBM officially classifies this function as optional
 */
inline static u32 handle_int13_0A(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    u8 sectors_to_read = AL;
    if (sectors_to_read == 0) return DISK_RET_EPARAM;

    u8 drive_num = DL;
    drive_t *drive = getDrive(drive_num);
    if (!drive) return DISK_RET_NO_MEDIA;

    // Распаковка CHS
    u16 cylinder = ((ecx >> 6) & 0x300) | (ecx & 0xFF);   // bits 6-7 (CL hi) + CH
    u8 sector = ecx & 0x3F; // CL low 6 bits
    u8 head = (edx >> 8) & 0xFF;

    if (sector == 0 || head > drive->pchs.head || cylinder > drive->pchs.cylinder)
        return DISK_RET_EPARAM;

    // Вычисляем LBA
    u32 lba = (cylinder * (drive->pchs.head + 1) + head) * (drive->pchs.sector) + (sector - 1);

    if ((u64)lba + sectors_to_read > drive->sectors)
        return DISK_RET_EBOUNDARY;
    
    FSIZE_t offset = (FSIZE_t)lba * drive->blksize;
    if (f_lseek(drive->pf, offset) != FR_OK) {
        return DISK_RET_ECONTROLLER;
    }
    
    u8 *buffer = X86_FAR_PTR(X86_ES, BX);

    u32 sectors_read = 0;
    for (; sectors_read < sectors_to_read; ++sectors_read) {
        UINT br;
        if (f_read(drive->pf, buffer, drive->blksize, &br) != FR_OK || br != drive->blksize) {
            return DISK_RET_ECONTROLLER | sectors_read; // AH | AL
        }
        buffer += drive->blksize;
    }
    return DISK_RET_SUCCESS | sectors_read; // AH | AL
}

/**
 * HARD DISK - WRITE LONG SECTOR(S) (AT and later)
 * AH = 0Bh
 * AL = number of sectors (01h may be only value supported)
 * CH = low eight bits of cylinder number
 * CL = sector number (bits 5-0)
 * high two bits of cylinder number (bits 7-6)
 * DH = head number
 * DL = drive number (80h = first, 81h = second)
 * ES:BX -> data buffer
 * 
 * Returns:
 * CF clear if successful
 * CF set on error
 * AH = status (see #00234)
 * AL = number of sectors transferred
 * 
 * Notes: Each sector's worth of data must be followed by four to seven bytes of error-correction information. Used for diagnostics only on PS/2 systems; IBM officially classifies this function as optional
 */
inline static u32 handle_int13_0B(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    u8 sectors_to_read = AL;
    if (sectors_to_read == 0) return DISK_RET_EPARAM;

    u8 drive_num = DL;
    drive_t *drive = getDrive(drive_num);
    if (!drive) return DISK_RET_NO_MEDIA;

    // Распаковка CHS
    u16 cylinder = ((ecx >> 6) & 0x300) | (ecx & 0xFF);   // bits 6-7 (CL hi) + CH
    u8 sector = ecx & 0x3F; // CL low 6 bits
    u8 head = (edx >> 8) & 0xFF;

    if (sector == 0 || head > drive->pchs.head || cylinder > drive->pchs.cylinder)
        return DISK_RET_EPARAM;

    // Вычисляем LBA
    u32 lba = (cylinder * (drive->pchs.head + 1) + head) * (drive->pchs.sector) + (sector - 1);

    if ((u64)lba + sectors_to_read > drive->sectors)
        return DISK_RET_EBOUNDARY;
    
    FSIZE_t offset = (FSIZE_t)lba * drive->blksize;
    if (f_lseek(drive->pf, offset) != FR_OK) {
        return DISK_RET_ECONTROLLER;
    }
    
    u8 *buffer = X86_FAR_PTR(X86_ES, BX);

    u32 sectors_read = 0;
    for (; sectors_read < sectors_to_read; ++sectors_read) {
        UINT br;
        if (f_write(drive->pf, buffer, drive->blksize, &br) != FR_OK || br != drive->blksize) {
            return DISK_RET_ECONTROLLER | sectors_read; // AH | AL
        }
        buffer += drive->blksize;
    }
    f_sync(drive->pf);
    return DISK_RET_SUCCESS | sectors_read; // AH | AL
}

inline static u32 handle_int13_0E(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    u8 sectors_to_read = AL;
    if (sectors_to_read != 1) return DISK_RET_EPARAM;  // Only one sector supported
    
    drive_t *drive = getDrive(DL);
    if (!drive) return DISK_RET_NO_MEDIA;

    // Распаковка CHS
    u16 cylinder = CH;
    u8 sector = CL & 0x3F;  // Сектора 1-63
    u8 head = DH;

    if (sector == 0 || head > drive->lchs.head || cylinder > drive->lchs.cylinder)
        return DISK_RET_EPARAM;

    // Вычисляем LBA
    u32 lba = ((u32)cylinder * drive->lchs.head + head) * drive->lchs.sector + (sector - 1);
    if (lba + sectors_to_read > drive->sectors)
        return DISK_RET_EBOUNDARY;

    FSIZE_t offset = (FSIZE_t)lba * DISK_SECTOR_SIZE;
    if (f_lseek(drive->pf, offset) != FR_OK) {
        return DISK_RET_EPARAM;
    }

    u8 *buffer = X86_FAR_PTR(X86_ES, BX);
    UINT br;
    if (f_read(drive->pf, buffer, DISK_SECTOR_SIZE, &br) != FR_OK) {
        return DISK_RET_EPARAM;
    }

    return DISK_RET_SUCCESS | sectors_to_read;
}

inline static u32 handle_int13_0F(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    u8 sectors_to_write = AL;
    if (sectors_to_write != 1) return DISK_RET_EPARAM;  // Только один сектор поддерживается

    drive_t *drive = getDrive(DL);
    if (!drive) return DISK_RET_NO_MEDIA;

    // Распаковка CHS
    u16 cylinder = CH;
    u8 sector = CL & 0x3F;  // Сектора 1-63
    u8 head = DH;

    if (sector == 0 || head > drive->lchs.head || cylinder > drive->lchs.cylinder)
        return DISK_RET_EPARAM;

    // Вычисляем LBA
    u32 lba = ((u32)cylinder * drive->lchs.head + head) * drive->lchs.sector + (sector - 1);
    if (lba + sectors_to_write > drive->sectors)
        return DISK_RET_EBOUNDARY;

    FSIZE_t offset = (FSIZE_t)lba * DISK_SECTOR_SIZE;
    if (f_lseek(drive->pf, offset) != FR_OK) {
        return DISK_RET_EPARAM;
    }

    u8 *buffer = X86_FAR_PTR(X86_ES, BX);
    UINT br;
    if (f_write(drive->pf, buffer, DISK_SECTOR_SIZE, &br) != FR_OK) {
        return DISK_RET_EPARAM;
    }

    f_sync(drive->pf);
    return DISK_RET_SUCCESS | sectors_to_write;
}

inline static u32 handle_int13_10(u32 edx) {
    drive_t *drive = getDrive(DL);  // Получаем информацию о диске по номеру из DL
    if (!drive) return DISK_RET_NO_MEDIA;  // Если диск не найден, возвращаем ошибку
    if (drive->type == DTYPE_FLOPPY) return DISK_RET_EPARAM;
    return DISK_RET_SUCCESS;
}

/**
 * DISK - GET DISK TYPE (XT 1986/1/10 or later,XT286,AT,PS)
 * AH = 15h
 * DL = drive number (bit 7 set for hard disk)
 * (AL = FFh, CX = FFFFh, see Note)
 * 
 * Return:
 * CF clear if successful
 * AH = type code
 * 00h no such drive
 * (SpeedStor) AL = 03h hard disk
 * CX:DX = number of 512-byte sectors
 * 01h floppy without change-line support
 * 02h floppy (or other removable drive) with change-line support
 * 03h hard disk
 * CX:DX = number of 512-byte sectors
 * CF set on error
 * AH = status (see #00234 at AH=01h)
 */
inline static u32 handle_int13_15(u32 edx) {
    u8 drive_num = DL;
    drive_t *drive = getDrive(drive_num);
    if (!drive) {
        // Нет такого устройства
        return CF_ON;
    }

    // Если это жесткий диск
    if (drive->type == DTYPE_ATA) {
        // Возвращаем информацию о жестком диске
        // Тип устройства: 03h (жесткий диск)
        // CX:DX - количество секторов по 512 байт
        u32 h16 = drive->sectors >> 16;
        u32 l16 = drive->sectors & 0xFFFF;
        __asm volatile ("mov r6, %0" :: "r"(h16): "r6"); // R6 ← CX (CH:CL packed)
        __asm volatile ("mov r7, %0" :: "r"(l16): "r7"); // R7 ← DX (DH:DL packed)
        return 3 << 8;
    }
    if (drive->type == DTYPE_FLOPPY) {
        return 1 << 8;
    }
    return CF_ON;
}

inline static u32 disk_ret(u32 r) {
    last_op_res = r;
    if (r) r |= CF_ON;
    return r;
}

inline static u32 pure_disk_ret(u32 r) {
    last_op_res = r;
    return r;
}

u32 x86_int13_hanler_C(u32 eax, u32 ebx, u32 ecx, u32 edx) {
    if (!cd_card_mount) return disk_ret(DISK_RET_NO_MEDIA);
    u8 extdrive = DL;
    if (CONFIG_CDROM_EMU) {
        // TODO:
    }
    switch (AH) { // AH - function
        case 0:
            // RESET DISK SYSTEM (TODO: ??)
            return disk_ret(DISK_RET_SUCCESS);
        case 1:
            // GET STATUS OF LAST OPERATION
            return last_op_res;
        case 2:
            // READ SECTORS INTO MEMORY
            return pure_disk_ret(handle_int13_02(eax, ebx, ecx, edx));
        case 3:
            // WRITE DISK SECTOR(S)
            return pure_disk_ret(handle_int13_03(eax, ebx, ecx, edx));
        case 4:
            // VERIFY DISK SECTOR(S) (TODO: ??)
            return pure_disk_ret(AL);
        case 5:
            if (DL < 2) // FORMAT FLOPPY DRIVE
                return disk_ret(handle_int13_05_fdd(eax, ebx, ecx, edx));
            return disk_ret(handle_int13_05_hdd(eax, ebx, ecx, edx));
        case 6: // unsupported by several bioses
            return disk_ret(handle_int13_06(eax, ecx, edx));
        case 7:
            return disk_ret(handle_int13_07(eax, ebx, ecx, edx));
        case 8:
            return disk_ret(handle_int13_08(eax, ebx, ecx, edx));
        case 9: // HARD DISK - INITIALIZE CONTROLLER WITH DRIVE PARAMETERS (AT,PS)
            return disk_ret(DISK_RET_SUCCESS);
        case 0x0A:
            return disk_ret(handle_int13_0A(eax, ebx, ecx, edx));
        case 0x0B:
            return disk_ret(handle_int13_0B(eax, ebx, ecx, edx));
        case 0x0C:
        case 0x0D:
        case 0x14:
            return disk_ret(DISK_RET_SUCCESS);
        case 0x0E:
            return disk_ret(handle_int13_0E(eax, ebx, ecx, edx));
        case 0x0F:
            return disk_ret(handle_int13_0F(eax, ebx, ecx, edx));
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            return disk_ret(handle_int13_10(edx));
            return disk_ret(handle_int13_10(edx));
        case 0x15:
            return handle_int13_15(edx);
    }
    return disk_ret(DISK_RET_EPARAM);
}
