#include "qemu/osdep.h"
#include "hw/gevico/gpio/g233_gpio.h"
#include "hw/core/irq.h"
#include "system/runstate.h"

#define GPIO_DIR_OFF        0x00
#define GPIO_OUT_OFF        0x04
#define GPIO_IN_OFF         0x08
#define GPIO_IE_OFF         0x0c
#define GPIO_IS_OFF         0x10
#define GPIO_TRIG_OFF       0x14
#define GPIO_POL_OFF        0x18








static uint64_t g233_gpio_read(void *opaque, hwaddr offset, unsigned size)
{
    G233GPIOState *s = opaque;
    (void)size;

    switch (offset) {
    case GPIO_DIR_OFF:
        return s->dir;
    case GPIO_OUT_OFF:
        return s->out;
    case GPIO_IN_OFF:
        return s->in;
    case GPIO_IE_OFF:
        return s->ie;
    case GPIO_IS_OFF:
        return s->is;
    case GPIO_TRIG_OFF:
        return s->trig;
    case GPIO_POL_OFF:
        return s->pol;
    default:
        return 0;
    }
}
static void g233_gpio_update_irq(G233GPIOState *s)
{
    qemu_set_irq(s->irq, (s->is & s->ie) != 0);
}

static void g233_gpio_out_change(G233GPIOState *s, uint32_t value)
{
    uint32_t old = s->out;
    uint32_t new = value;
    uint32_t old_level = old;
    uint32_t new_level = new;
    uint32_t edge_rise = ~old_level & new_level;
    uint32_t edge_fall = old_level & ~new_level;
    uint32_t edge_event = (edge_rise & s->pol) | (edge_fall & ~s->pol);
    uint32_t level_event = (new_level & s->pol) | (~new_level & ~s->pol);

    s->out = new;
    s->in = new;
    
    uint32_t temp = s->is & ~s->trig;
    edge_event &= ~s->trig;
    s->is = (level_event & s->trig) | temp;


    s->is |= edge_event ;
    s->is &= s->ie;
    g233_gpio_update_irq(s);
}

static void g233_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    G233GPIOState *s = opaque;
    (void)size;

    switch (offset) {
    case GPIO_DIR_OFF:
        s->dir = value;
        break;
    case GPIO_OUT_OFF:
        g233_gpio_out_change(s, value);
        break;
    case GPIO_IE_OFF:
        s->ie = value;
        g233_gpio_update_irq(s);
        break;
    case GPIO_IS_OFF:
        /* W1C */
        s->is &= ~((uint32_t)value);
        g233_gpio_update_irq(s);
        break;
    case GPIO_TRIG_OFF:
        s->trig = value;
        break;
    case GPIO_POL_OFF:
        s->pol = value;
        break;
    default:
        return;
    }
}

static const MemoryRegionOps g233_gpio_ops = {
    .read = g233_gpio_read,
    .write = g233_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void g233_gpio_reset(DeviceState *dev)
{
    G233GPIOState *s = G233_GPIO(dev);

    s->dir = 0;
    s->out = 0;
    s->in = 0;
    s->ie = 0;
    s->is = 0;
    s->trig = 0;
    s->pol = 0;
    qemu_set_irq(s->irq, 0);
}

static void g233_gpio_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    G233GPIOState *s = G233_GPIO(obj);

    memory_region_init_io(&s->mmio, obj, &g233_gpio_ops, s, TYPE_G233_GPIO, 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);

    sysbus_init_irq(sbd, &s->irq);
}

static void g233_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "GEVICO G233 GPIO";
    device_class_set_legacy_reset(dc, g233_gpio_reset);
}

static const TypeInfo g233_gpio_info = {
    .name = TYPE_G233_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233GPIOState),
    .instance_init = g233_gpio_init,
    .class_init = g233_gpio_class_init,
};

static void g233_gpio_register_types(void)
{
    type_register_static(&g233_gpio_info);
}

type_init(g233_gpio_register_types);
