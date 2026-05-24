#ifndef HW_GEVICO_PWM_G233_H
#define HW_GEVICO_PWM_G233_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_G233_GPIO "gevico.g233-pwm"
OBJECT_DECLARE_SIMPLE_TYPE(G233PWMState, G233_PWM)

struct G233PWMState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t dir;
    uint32_t out;
    uint32_t in;
    uint32_t ie;
    uint32_t is;
    uint32_t trig;
    uint32_t pol;
};

#endif 
