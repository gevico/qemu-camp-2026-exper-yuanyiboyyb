#ifndef HW_GEVICO_SPI_G233_SPI_H
#define HW_GEVICO_SPI_G233_SPI_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "hw/gevico/flash/g233_flash.h"

#define TYPE_G233_SPI "gevico.g233-spi"
OBJECT_DECLARE_SIMPLE_TYPE(G233SPIState, G233_SPI)

struct G233SPIState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t cr1;
    uint32_t cr2;
    uint32_t sr;
    uint32_t dr;

    G233FLASHState* channels[4];
    uint8_t flag;
};
uint32_t g233_spi_flash_connect(G233SPIState *s, G233FLASHState *f);

#endif
