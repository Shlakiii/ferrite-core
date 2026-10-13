#include "ata_pio.h"

#include <io/io.h>
#include <io/printg.h>

static AtaDrive _drives[ATA_MAX_DRIVES];
static int      _drive_count = 0;

static inline uint8_t  _inb (uint16_t p) { return inb(p);  }
static inline void     _outb(uint16_t p, uint8_t v) { outb(p, v);}
static inline uint16_t _inw (uint16_t p) { return inw(p);  }
static inline void     _outw(uint16_t p, uint16_t v) { outw(p, v);}

static void _io_delay(uint16_t ctrl_base) {
    _inb(ctrl_base);
    _inb(ctrl_base);
    _inb(ctrl_base);
    _inb(ctrl_base);
}

static void _insw(uint16_t port, void* buf, uint32_t count) {
    uint16_t* p = (uint16_t*)buf;
    for (uint32_t i = 0; i < count; i++)
        p[i] = _inw(port);
}

static void _outsw(uint16_t port, const void* buf, uint32_t count) {
    const uint16_t* p = (const uint16_t*)buf;
    for (uint32_t i = 0; i < count; i++)
        _outw(port, p[i]);
}

static bool _wait_drq(uint16_t base) {
    for (int i = 0; i < ATA_TIMEOUT_ITER; i++) {
        uint8_t s = _inb(base + ATA_REG_STATUS);
        if (s & ATA_SR_ERR)  return false;
        if (s & ATA_SR_BSY)  continue;
        if (s & ATA_SR_DRQ)  return true;
    }
    return false;
}

static bool _wait_ready(uint16_t base) {
    for (int i = 0; i < ATA_TIMEOUT_ITER; i++) {
        uint8_t s = _inb(base + ATA_REG_STATUS);
        if (s & ATA_SR_ERR) return false;
        if (!(s & ATA_SR_BSY)) return true;
    }
    return false;
}

static void _soft_reset(uint16_t ctrl_base) {
    _outb(ctrl_base, 0x04);
    _io_delay(ctrl_base);
    _outb(ctrl_base, 0x00);
    _io_delay(ctrl_base);
}

static void _setup_lba28(const AtaDrive* d, uint32_t lba, uint16_t count) {
    uint16_t base = d->io_base;
    uint8_t  head = (uint8_t)(0xE0 | d->is_slave | ((lba >> 24) & 0x0F));

    _outb(base + ATA_REG_DRIVE,    head);
    _outb(base + ATA_REG_SECCOUNT, (uint8_t)count);
    _outb(base + ATA_REG_LBA_LO,   (uint8_t) lba);
    _outb(base + ATA_REG_LBA_MID,  (uint8_t)(lba >>  8));
    _outb(base + ATA_REG_LBA_HI,   (uint8_t)(lba >> 16));
}

static bool _identify(AtaDrive* d) {
    uint16_t base = d->io_base;

    _outb(base + ATA_REG_DRIVE, (uint8_t)(0xA0 | d->is_slave));
    _io_delay(d->ctrl_base);

    _outb(base + ATA_REG_SECCOUNT, 0);
    _outb(base + ATA_REG_LBA_LO,   0);
    _outb(base + ATA_REG_LBA_MID,  0);
    _outb(base + ATA_REG_LBA_HI,   0);

    _outb(base + ATA_REG_CMD, ATA_CMD_IDENTIFY);
    _io_delay(d->ctrl_base);

    uint8_t status = _inb(base + ATA_REG_STATUS);
    if (status == 0) return false;
    if (!_wait_ready(base)) return false;
    if (_inb(base + ATA_REG_LBA_MID) || _inb(base + ATA_REG_LBA_HI))
        return false;

    if (!_wait_drq(base)) return false;

    uint16_t buf[256];
    _insw(base + ATA_REG_DATA, buf, 256);

    d->lba_count = ((uint32_t)buf[61] << 16) | buf[60];

    for (int i = 0; i < 20; i++) {
        d->model[i * 2    ] = (char)(buf[27 + i] >> 8);
        d->model[i * 2 + 1] = (char)(buf[27 + i] & 0xFF);
    }
    d->model[40] = '\0';

    for (int i = 39; i >= 0 && d->model[i] == ' '; i--)
        d->model[i] = '\0';

    d->present = true;
    return true;
}

int ata_init(void) {
    static const uint16_t io_bases[] = { ATA_PRIMARY_IO,   ATA_SECONDARY_IO };
    static const uint16_t ctrl_bases[] = { ATA_PRIMARY_CTRL, ATA_SECONDARY_CTRL };

    _drive_count = 0;

    for (int ch = 0; ch < 2; ch++) {
        _soft_reset(ctrl_bases[ch]);

        for (int sl = 0; sl < 2; sl++) {
            if (_drive_count >= ATA_MAX_DRIVES) break;

            AtaDrive* d = &_drives[_drive_count];
            d->io_base   = io_bases[ch];
            d->ctrl_base = ctrl_bases[ch];
            d->is_slave  = sl ? 0x10 : 0x00;
            d->present   = false;
            d->lba_count = 0;
            d->model[0] = '\0';

            if (_identify(d)) {
                uint32_t size_mb = d->lba_count / 2048;
                log(3, "ATA%d: %s [%u MB, %u sectors]", _drive_count, d->model, (unsigned)size_mb, (unsigned)d->lba_count);
                _drive_count++;
            }
        }
    }

    if (_drive_count == 0)
        log(2, "ATA: no drives found");
    else
        log(3, "ATA: %d drive(s) detected", _drive_count);

    return _drive_count;
}

int ata_drive_count(void) {
    return _drive_count;
}

const AtaDrive* ata_get_drive(int index) {
    if (index < 0 || index >= _drive_count) return 0;
    return &_drives[index];
}

bool ata_read_sectors(int drive, uint32_t lba, uint16_t count, void* buf) {
    if (drive < 0 || drive >= _drive_count) return false;
    if (!buf || count == 0)                 return false;

    const AtaDrive* d = &_drives[drive];
    if (!d->present)                            return false;
    if ((uint32_t)(lba + count) > d->lba_count) return false;

    uint16_t base = d->io_base;
    uint8_t* dst  = (uint8_t*)buf;

    for (uint16_t i = 0; i < count; i++, lba++) {
        _outb(base + ATA_REG_DRIVE, (uint8_t)(0xE0 | d->is_slave | ((lba >> 24) & 0x0F)));
        _io_delay(d->ctrl_base);

        if (!_wait_ready(base)) return false;

        _setup_lba28(d, lba, 1);
        _outb(base + ATA_REG_CMD, ATA_CMD_READ_PIO);
        _io_delay(d->ctrl_base);

        if (!_wait_drq(base)) return false;
        if (_inb(base + ATA_REG_STATUS) & ATA_SR_ERR) return false;

        _insw(base + ATA_REG_DATA, dst, ATA_SECTOR_SIZE / 2);
        dst += ATA_SECTOR_SIZE;
    }
    return true;
}

bool ata_write_sectors(int drive, uint32_t lba, uint16_t count, const void* buf) {
    if (drive < 0 || drive >= _drive_count)     return false;
    if (!buf || count == 0)                     return false;

    const AtaDrive* d = &_drives[drive];
    if (!d->present)                            return false;
    if ((uint32_t)(lba + count) > d->lba_count) return false;

    uint16_t       base = d->io_base;
    const uint8_t* src  = (const uint8_t*)buf;

    for (uint16_t i = 0; i < count; i++, lba++) {
        _outb(base + ATA_REG_DRIVE, (uint8_t)(0xE0 | d->is_slave | ((lba >> 24) & 0x0F)));
        _io_delay(d->ctrl_base);

        if (!_wait_ready(base)) return false;

        _setup_lba28(d, lba, 1);
        _outb(base + ATA_REG_CMD, ATA_CMD_WRITE_PIO);
        _io_delay(d->ctrl_base);

        if (!_wait_drq(base)) return false;

        _outsw(base + ATA_REG_DATA, src, ATA_SECTOR_SIZE / 2);
        src += ATA_SECTOR_SIZE;
    }

    _outb(base + ATA_REG_DRIVE, (uint8_t)(0xE0 | d->is_slave));
    _outb(base + ATA_REG_CMD, ATA_CMD_CACHE_FLUSH);
    _wait_ready(base);

    return true;
}
