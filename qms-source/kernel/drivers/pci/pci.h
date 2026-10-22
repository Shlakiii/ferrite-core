#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
} pci_dev_t;

#ifdef __cplusplus
extern "C" {
#endif

void     pci_init();
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
void     pci_write(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t data);

uint16_t pci_get_vendor(uint8_t bus, uint8_t device, uint8_t func);
uint32_t pci_get_bar(uint8_t bus, uint8_t device, uint8_t func, uint8_t bar_index);

#ifdef __cplusplus
}
#endif

#endif
