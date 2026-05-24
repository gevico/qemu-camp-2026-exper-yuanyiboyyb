/*
 * G233 Watchdog Timer (WDT)
 *
 * Skeleton header.
 */

#ifndef HW_GEVICO_WATCHDOG_G233_WDT_H
#define HW_GEVICO_WATCHDOG_G233_WDT_H

#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_G233_WDT "gevico.g233-wdt"
OBJECT_DECLARE_SIMPLE_TYPE(G233WDTState, G233_WDT)

struct G233WDTState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    QEMUTimer *timer;

    uint32_t ctrl;
    uint32_t load;
    uint32_t val;
    uint32_t sr;
    uint32_t key;
    int64_t expire_ns;
};

#endif /* HW_GEVICO_WATCHDOG_G233_WDT_H */
