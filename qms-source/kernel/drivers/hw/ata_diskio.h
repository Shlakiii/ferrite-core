#ifndef _HW_ATA_DISKIO_H
#define _HW_ATA_DISKIO_H

#include "ata_pio.h"

#include <func/vfs.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ata_read_sectors (int drive, uint32_t lba, uint16_t count, void* buf);
bool ata_write_sectors(int drive, uint32_t lba, uint16_t count, const void* buf);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class AtaDiskIO : public IDiskIO {
public:
    explicit AtaDiskIO(int drive_index)
        : drive_idx_(drive_index) {}

    bool read_sectors(uint32_t lba, void* buf, uint16_t count) override {
        return ata_read_sectors(drive_idx_, lba, count, buf);
    }

    bool write_sectors(uint32_t lba, const void* buf, uint16_t count) override {
        return ata_write_sectors(drive_idx_, lba, count, buf);
    }

    bool present() const {
        const AtaDrive* d = ata_get_drive(drive_idx_);
        return d && d->present;
    }

    uint32_t sector_count() const {
        const AtaDrive* d = ata_get_drive(drive_idx_);
        return d ? d->lba_count : 0;
    }

    const char* model() const {
        const AtaDrive* d = ata_get_drive(drive_idx_);
        return d ? d->model : "(none)";
    }

private:
    int drive_idx_;
};

#endif

#endif
