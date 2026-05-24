/*
 * G233 Watchdog Timer (WDT)
 *
 * Skeleton file.
 */

#include "qemu/osdep.h"
#include "hw/gevico/watchdog/g233_wdt.h"
#include "hw/core/irq.h"
#include "system/watchdog.h"
#include "system/runstate.h"

#define WDT_CTRL_OFF        0x00
#define WDT_LOAD_OFF        0x04
#define WDT_VAL_OFF         0x08
#define WDT_SR_OFF          0x0c
#define WDT_KEY_OFF         0x10

#define WDT_CTRL_EN         BIT(0)
#define WDT_CTRL_INTEN      BIT(1)
#define WDT_CTRL_RSTEN      BIT(2)
#define WDT_CTRL_LOCK       BIT(3)
#define WDT_CTRL_WR_MASK    (WDT_CTRL_EN | WDT_CTRL_INTEN | WDT_CTRL_RSTEN)
#define WDT_CTRL_RD_MASK    (WDT_CTRL_WR_MASK | WDT_CTRL_LOCK)

#define WDT_SR_TIMEOUT      BIT(0)
#define WDT_SR_RD_MASK      WDT_SR_TIMEOUT

#define WDT_KEY_FEED        0x5a5a5a5aU
#define WDT_KEY_LOCK        0x1acce551U

/* 1 tick = 1us */
#define WDT_TICK_NS         1000LL

static uint32_t g233_wdt_current_val(G233WDTState *s)
{
    int64_t now_ns;
    int64_t remain_ns;

    if (!(s->ctrl & WDT_CTRL_EN)) {
        return s->val;
    }
    if (s->sr & WDT_SR_TIMEOUT) {
        return 0;
    }

    now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    remain_ns = s->expire_ns - now_ns;
    if (remain_ns <= 0) {
        return 0;
    }

    return (uint32_t)DIV_ROUND_UP(remain_ns, WDT_TICK_NS);
}

static void g233_wdt_start_countdown(G233WDTState *s)
{
    int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t delta_ns = (int64_t)s->load * WDT_TICK_NS;

    s->val = s->load;
    s->expire_ns = now_ns + delta_ns;
    timer_mod(s->timer, s->expire_ns);
}

static void g233_wdt_stop_countdown(G233WDTState *s)
{
    s->val = g233_wdt_current_val(s);
    timer_del(s->timer);
}

static void g233_wdt_timeout(void *opaque)
{
    G233WDTState *s = opaque;

    s->val = 0;
    s->sr |= WDT_SR_TIMEOUT;

    if (s->ctrl & WDT_CTRL_INTEN) {
        qemu_set_irq(s->irq, 1);
    }
    if (s->ctrl & WDT_CTRL_RSTEN) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void g233_wdt_write_ctrl(G233WDTState *s, uint32_t value)
{
    uint32_t old_ctrl;
    uint32_t new_ctrl;

    if (s->ctrl & WDT_CTRL_LOCK) {
        return;
    }

    old_ctrl = s->ctrl;
    new_ctrl = (s->ctrl & WDT_CTRL_LOCK) | (value & WDT_CTRL_WR_MASK);
    s->ctrl = new_ctrl;

    /*
     * EN 0->1: start countdown from current LOAD value.
     * EN 1->0: stop watchdog (timer handling is added with full countdown path).
     */
    if (!(old_ctrl & WDT_CTRL_EN) && (new_ctrl & WDT_CTRL_EN)) {
        g233_wdt_start_countdown(s);
    } else if ((old_ctrl & WDT_CTRL_EN) && !(new_ctrl & WDT_CTRL_EN)) {
        g233_wdt_stop_countdown(s);
    }
}

static uint64_t g233_wdt_read(void *opaque, hwaddr offset, unsigned size)
{
    G233WDTState *s = opaque;
    (void)size;

    switch (offset) {
    case WDT_CTRL_OFF:
        return s->ctrl & WDT_CTRL_RD_MASK;
    case WDT_LOAD_OFF:
        return s->load;
    case WDT_VAL_OFF:
        return g233_wdt_current_val(s);
    case WDT_SR_OFF:
        return s->sr & WDT_SR_RD_MASK;
    case WDT_KEY_OFF:
        /* KEY is write-only; reads return 0. */
        return 0;
    default:
        return 0;
    }
}

static void g233_wdt_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    G233WDTState *s = opaque;
    (void)size;

    switch (offset) {
    case WDT_CTRL_OFF:
        g233_wdt_write_ctrl(s, value);
        return;
    case WDT_LOAD_OFF:
        if (s->ctrl & WDT_CTRL_LOCK) {
            return;
        }
        s->load = value;
        return;
    case WDT_VAL_OFF:
        /* Read-only register: writes are ignored. */
        return;
    case WDT_SR_OFF:
        /* W1C: write 1 clears TIMEOUT, write 0 leaves it unchanged. */
        if (value & WDT_SR_TIMEOUT) {
            s->sr &= ~WDT_SR_TIMEOUT;
            qemu_set_irq(s->irq, 0);
        }
        return;
    case WDT_KEY_OFF:
        s->key = value;
        if ((uint32_t)value == WDT_KEY_FEED) {
            /* Feed: reload counter and clear timeout flag. */
            s->sr &= ~WDT_SR_TIMEOUT;
            qemu_set_irq(s->irq, 0);
            if (s->ctrl & WDT_CTRL_EN) {
                g233_wdt_start_countdown(s);
            } else {
                s->val = s->load;
            }
        } else if ((uint32_t)value == WDT_KEY_LOCK) {
            /* Lock: CTRL becomes read-only until reset. */
            s->ctrl |= WDT_CTRL_LOCK;
        }
        return;
    default:
        return;
    }
}

static const MemoryRegionOps g233_wdt_ops = {
    .read = g233_wdt_read,
    .write = g233_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void g233_wdt_reset(DeviceState *dev)
{
    G233WDTState *s = G233_WDT(dev);

    s->ctrl = 0;
    s->load = 0x0000ffff;
    s->val = 0x0000ffff;
    s->sr = 0;
    s->key = 0;
    s->expire_ns = 0;
    qemu_set_irq(s->irq, 0);
    timer_del(s->timer);
}

static void g233_wdt_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    G233WDTState *s = G233_WDT(obj);

    sysbus_init_mmio(sbd, &s->mmio);
    memory_region_init_io(&s->mmio, obj, &g233_wdt_ops, s, TYPE_G233_WDT, 0x1000);
    sysbus_init_irq(sbd, &s->irq);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, g233_wdt_timeout, s);
}

static void g233_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "GEVICO G233 Watchdog Timer";
    device_class_set_legacy_reset(dc, g233_wdt_reset);
}

static const TypeInfo g233_wdt_info = {
    .name = TYPE_G233_WDT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233WDTState),
    .instance_init = g233_wdt_init,
    .class_init = g233_wdt_class_init,
};

static void g233_wdt_register_types(void)
{
    type_register_static(&g233_wdt_info);
}

type_init(g233_wdt_register_types);
