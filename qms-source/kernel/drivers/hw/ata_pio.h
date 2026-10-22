#ifndef _HW_ATA_PIO_H
#define _HW_ATA_PIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTRL  0x376

#define ATA_REG_DATA        0
#define ATA_REG_ERROR       1
#define ATA_REG_FEATURES    1
#define ATA_REG_SECCOUNT    2
#define ATA_REG_LBA_LO      3
#define ATA_REG_LBA_MID     4
#define ATA_REG_LBA_HI      5
#define ATA_REG_DRIVE       6
#define ATA_REG_STATUS      7
#define ATA_REG_CMD         7

#define ATA_SR_BSY          0x80
#define ATA_SR_DRDY         0x40
#define ATA_SR_DRQ          0x08
#define ATA_SR_ERR          0x01

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY    0xEC

#define ATA_SECTOR_SIZE     512
#define ATA_MAX_DRIVES      4
#define ATA_TIMEOUT_ITER    400000

typedef struct AtaDrive {
    bool     present;
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t  is_slave;
    uint32_t lba_count;
    char     model[41];
} AtaDrive;

#ifdef __cplusplus
extern "C" {
#endif

int  ata_init(void);
int  ata_drive_count(void);
const AtaDrive* ata_get_drive(int index);
bool ata_read_sectors (int drive, uint32_t lba, uint16_t count, void* buf);
bool ata_write_sectors(int drive, uint32_t lba, uint16_t count, const void* buf);

#ifdef __cplusplus
}
#endif

#endif
