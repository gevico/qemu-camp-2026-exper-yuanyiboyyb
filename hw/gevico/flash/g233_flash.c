#include "qemu/osdep.h"
#include "hw/gevico/flash/g233_flash.h"
#include "hw/core/qdev-properties.h"

#define G233_FLASH_SECTOR_SIZE 4096u
#define G233_FLASH_PAGE_SIZE   256u

static inline bool g233_flash_addr_valid(const G233FLASHState *s, uint32_t addr)
{
    return s->store && s->size && addr < s->size;
}

static inline uint8_t g233_flash_cmd_jedec_id(G233FLASHState *s,uint32_t shift)
{
    return (s->jedec_id >> shift) & 0xff; 
}

static inline uint8_t g233_flash_cmd_rdsr(G233FLASHState *s)
{
    return s->status;
}

static inline void g233_flash_cmd_wren(G233FLASHState *s)
{
    s->status |= G233_FLASH_SR_WEL;
}

static inline void g233_flash_cmd_wrdi(G233FLASHState *s)
{
    s->status &= ~G233_FLASH_SR_WEL;
}

static void g233_flash_cmd_se(G233FLASHState *s, uint32_t addr)
{
    uint32_t base;

    if (!(s->status & G233_FLASH_SR_WEL) || !g233_flash_addr_valid(s, addr)) {
        return;
    }

    base = addr & ~(G233_FLASH_SECTOR_SIZE - 1);
    memset(&s->store[base], 0xff, MIN(G233_FLASH_SECTOR_SIZE, s->size - base));
    s->status &= ~G233_FLASH_SR_WEL;
}

static void g233_flash_cmd_pp_begin(G233FLASHState *s, uint32_t addr)
{
    uint32_t page_base;

    s->op_addr = addr;
    page_base = addr & ~(G233_FLASH_PAGE_SIZE - 1);
    s->pp_page_end = page_base + G233_FLASH_PAGE_SIZE;
}
static void g233_flash_cmd_pp_write_byte(G233FLASHState *s, uint8_t value)
{

    if (!(s->status & G233_FLASH_SR_WEL) || !g233_flash_addr_valid(s, s->op_addr)) {
        return;
    }

    if (s->op_addr >= s->pp_page_end) {
        return;
    }
    s->store[s->op_addr] &= value;
    s->op_addr++;
}

static void g233_flash_cmd_read_begin(G233FLASHState *s, uint32_t addr)
{
    s->op_addr = addr;
}

static uint8_t g233_flash_cmd_read_byte(G233FLASHState *s)
{
    uint8_t value = 0xff;

    if (!g233_flash_addr_valid(s, s->op_addr)) {
        return value;
    }

    value = s->store[s->op_addr];
    s->op_addr++;
    return value;
}

void g233_flash_xfer_cs_down(G233FLASHState *s)
{
    if (!s) {
        return;
    }

    switch (s->xfer_state) {
    case G233_FLASH_XFER_PP_STREAM:
        s->pp_page_end = UINT32_MAX;
        s->status &= ~G233_FLASH_SR_WEL;
        break;
    case G233_FLASH_XFER_READ_STREAM:
    case G233_FLASH_XFER_RDSR:
    default:
        break;
    }

    s->xfer_state = G233_FLASH_XFER_IDLE;
}
uint8_t g233_flash_xfer_byte(G233FLASHState *s, uint8_t tx)
{

    switch (s->xfer_state) {
    case G233_FLASH_XFER_IDLE:
        switch (tx) {
        case G233_FLASH_CMD_JEDEC_ID:
            s->xfer_state = G233_FLASH_XFER_JEDEC_ID_B0;
            return 0;
        case G233_FLASH_CMD_RDSR:
            s->xfer_state = G233_FLASH_XFER_RDSR;
            return 0;
        case G233_FLASH_CMD_WREN:
            g233_flash_cmd_wren(s);
            return 0;
        case G233_FLASH_CMD_WRDI:
            g233_flash_cmd_wrdi(s);
            return 0;
        case G233_FLASH_CMD_SE:
            s->addr_latch = 0;
            s->xfer_state = G233_FLASH_XFER_SE_ADDR_B0;
            return 0;
        case G233_FLASH_CMD_PP:
            s->addr_latch = 0;
            s->xfer_state = G233_FLASH_XFER_PP_ADDR_B0;
            return 0;
        case G233_FLASH_CMD_READ:
            s->addr_latch = 0;
            s->xfer_state = G233_FLASH_XFER_READ_ADDR_B0;
            return 0;
        default:
            return 0;
        }
    case G233_FLASH_XFER_JEDEC_ID_B0:
        s->xfer_state = G233_FLASH_XFER_JEDEC_ID_B1;
        return g233_flash_cmd_jedec_id(s,16);
    case G233_FLASH_XFER_JEDEC_ID_B1:
        s->xfer_state = G233_FLASH_XFER_JEDEC_ID_B2;
        return g233_flash_cmd_jedec_id(s,8);
    case G233_FLASH_XFER_JEDEC_ID_B2:
        s->xfer_state = G233_FLASH_XFER_IDLE;
        return g233_flash_cmd_jedec_id(s,0);

    case G233_FLASH_XFER_RDSR:
        return g233_flash_cmd_rdsr(s);

    case G233_FLASH_XFER_READ_ADDR_B0:
        s->addr_latch = ((uint32_t)tx) << 16;
        s->xfer_state = G233_FLASH_XFER_READ_ADDR_B1;
        return 0;
    case G233_FLASH_XFER_READ_ADDR_B1:
        s->addr_latch |= ((uint32_t)tx) << 8;
        s->xfer_state = G233_FLASH_XFER_READ_ADDR_B2;
        return 0;
    case G233_FLASH_XFER_READ_ADDR_B2:
        s->addr_latch |= tx;
        g233_flash_cmd_read_begin(s, s->addr_latch);
        s->xfer_state = G233_FLASH_XFER_READ_STREAM;
        return 0;
    case G233_FLASH_XFER_READ_STREAM:
        return g233_flash_cmd_read_byte(s);

    case G233_FLASH_XFER_PP_ADDR_B0:
        s->addr_latch = ((uint32_t)tx) << 16;
        s->xfer_state = G233_FLASH_XFER_PP_ADDR_B1;
        return 0;
    case G233_FLASH_XFER_PP_ADDR_B1:
        s->addr_latch |= ((uint32_t)tx) << 8;
        s->xfer_state = G233_FLASH_XFER_PP_ADDR_B2;
        return 0;
    case G233_FLASH_XFER_PP_ADDR_B2:
        s->addr_latch |= tx;
        g233_flash_cmd_pp_begin(s, s->addr_latch);
        s->xfer_state = G233_FLASH_XFER_PP_STREAM;
        return 0;
    case G233_FLASH_XFER_PP_STREAM:
        g233_flash_cmd_pp_write_byte(s, tx);
        return 0;

    case G233_FLASH_XFER_SE_ADDR_B0:
        s->addr_latch = ((uint32_t)tx) << 16;
        s->xfer_state = G233_FLASH_XFER_SE_ADDR_B1;
        return 0;
    case G233_FLASH_XFER_SE_ADDR_B1:
        s->addr_latch |= ((uint32_t)tx) << 8;
        s->xfer_state = G233_FLASH_XFER_SE_ADDR_B2;
        return 0;
    case G233_FLASH_XFER_SE_ADDR_B2:
        s->addr_latch |= tx;
        g233_flash_cmd_se(s, s->addr_latch);
        s->xfer_state = G233_FLASH_XFER_IDLE;
        return 0;

    default:
        s->xfer_state = G233_FLASH_XFER_IDLE;
        return 0;
    }
}

static const Property g233_flash_props[] = {
    DEFINE_PROP_UINT32("size",     G233FLASHState, size,     2 * 1024 * 1024),
    DEFINE_PROP_UINT32("jedec-id", G233FLASHState, jedec_id, 0xEF3015),
};

static void g233_flash_init(Object *obj)
{
    G233FLASHState *s = G233_FLASH(obj);
    s->xfer_state = G233_FLASH_XFER_IDLE;
    s->status = 0;
    s->op_addr = 0;
    s->addr_latch = 0;
    s->pp_page_end = UINT32_MAX;
    s->store = NULL;
}

static void g233_flash_realize(DeviceState *dev, Error **errp)
{
    G233FLASHState *s = G233_FLASH(dev);

    s->store = g_malloc0(s->size);
    memset(s->store, 0xFF, s->size);
}

static void g233_flash_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_props(dc, g233_flash_props);
    dc->realize = g233_flash_realize;
    dc->desc = "GEVICO G233 FLASH";
}

static const TypeInfo g233_flash_info = {
    .name = TYPE_G233_FLASH,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(G233FLASHState),
    .instance_init = g233_flash_init,
    .class_init = g233_flash_class_init,
};

static void g233_flash_register_types(void)
{
    type_register_static(&g233_flash_info);
}

type_init(g233_flash_register_types);
