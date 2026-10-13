#include "pci.h"

#include <io/io.h>

uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t data) {
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, data);
}

uint16_t pci_get_vendor(uint8_t bus, uint8_t device, uint8_t func) {
    uint32_t res = pci_read(bus, device, func, 0x00);
    return (uint16_t)(res & 0xFFFF);
}

uint32_t pci_get_bar(uint8_t bus, uint8_t device, uint8_t func, uint8_t bar_index) {
    if (bar_index > 5) return 0;
    return pci_read(bus, device, func, 0x10 + (bar_index * 4));
}

void pci_on_device_found(uint8_t bus, uint8_t device, uint8_t func) {
    uint32_t id_reg = pci_read(bus, device, func, 0x00);
    uint32_t class_reg = pci_read(bus, device, func, 0x08);
    
    uint16_t vendor = (uint16_t)(id_reg & 0xFFFF);
    uint16_t device_id = (uint16_t)(id_reg >> 16);
    uint8_t class_code = (uint8_t)(class_reg >> 24);
    uint8_t subclass = (uint8_t)(class_reg >> 16);
}

void pci_scan() {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            if (pci_get_vendor(bus, dev, 0) == 0xFFFF) continue;

            pci_on_device_found(bus, dev, 0);

            uint8_t header = (uint8_t)(pci_read(bus, dev, 0, 0x0C) >> 16);
            if (header & 0x80) {
                for (int func = 1; func < 8; func++) {
                    if (pci_get_vendor(bus, dev, func) != 0xFFFF) {
                        pci_on_device_found(bus, dev, func);
                    }
                }
            }
        }
    }
}

void pci_init() {
    pci_scan();
}
