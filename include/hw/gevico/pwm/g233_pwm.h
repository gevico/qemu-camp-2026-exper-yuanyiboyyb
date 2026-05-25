#ifndef HW_GEVICO_PWM_G233_H
#define HW_GEVICO_PWM_G233_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "qemu/timer.h"

#define TYPE_G233_PWM "gevico.g233-pwm"
OBJECT_DECLARE_SIMPLE_TYPE(G233PWMState, G233_PWM)

typedef struct G233PWMChannel{
    uint32_t index;
    uint32_t ctrl;
    uint32_t preiod;
    uint32_t duty;
    uint32_t cnt;
    int64_t expire_ns;
    QEMUTimer *timer;
}G233PWMChannel;

struct G233PWMState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t glb;
    G233PWMChannel channels[4];
};

#endif 
