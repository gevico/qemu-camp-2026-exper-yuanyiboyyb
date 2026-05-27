#include "qemu/osdep.h"
#include "hw/gevico/spi/g233_spi.h"
#include "hw/core/irq.h"

#define SPI_CR1_OFF 0x00
#define SPI_CR2_OFF 0x04
#define SPI_SR_OFF  0x08
#define SPI_DR_OFF  0x0c

#define SPI_CR1_SPE    BIT(0)
#define SPI_CR1_MSTR   BIT(2)
#define SPI_CR1_ERRIE  BIT(5)
#define SPI_CR1_RXNEIE BIT(6)
#define SPI_CR1_TXEIE  BIT(7)
#define SPI_CR1_WR_MASK (SPI_CR1_SPE | SPI_CR1_MSTR | SPI_CR1_ERRIE | \
                         SPI_CR1_RXNEIE | SPI_CR1_TXEIE)

#define SPI_CR2_CS_SEL_MASK 0x3

#define SPI_SR_RXNE    BIT(0)
#define SPI_SR_TXE     BIT(1)
#define SPI_SR_OVERRUN BIT(4)


uint32_t g233_spi_flash_connect(G233SPIState *s,G233FLASHState* f){
    for(uint32_t i=0;i<4;i++){
        if (((s->flag >> i) & 0x1) == 0) {
            s->channels[i] = f;
            s->flag |= 1<<i;
            return 1;
        }
    }
    return 0;
}
static void g233_spi_update_irq_overrun(G233SPIState *s)
{
    bool irq = false;
    if ((s->cr1 & SPI_CR1_SPE)&&(s->cr1 & SPI_CR1_ERRIE)) {
            irq = true;
    }
    qemu_set_irq(s->irq, irq);
}
static void g233_spi_update_irq_txe(G233SPIState *s)
{
    bool irq = false;
    if ((s->cr1 & SPI_CR1_SPE)&&(s->cr1 & SPI_CR1_TXEIE) && (s->sr & SPI_SR_TXE)) {
            irq = true;
    }
    qemu_set_irq(s->irq, irq);
}
static void g233_spi_update_irq_rxne(G233SPIState *s)
{
    bool irq = false;
    if ((s->cr1 & SPI_CR1_SPE)&&(s->cr1 & SPI_CR1_RXNEIE) && (s->sr & SPI_SR_RXNE)) {
            irq = true;
    }
    qemu_set_irq(s->irq, irq);
}

static void g233_spi_write_cr1(G233SPIState *s, uint32_t value)
{
    s->cr1 = value & SPI_CR1_WR_MASK;

    if (!(s->cr1 & SPI_CR1_SPE)) {
        qemu_set_irq(s->irq, 0);
        return;
    }

    /* enable 后立即按当前 SR 评估中断 */
    g233_spi_update_irq_txe(s);
    g233_spi_update_irq_rxne(s);
    g233_spi_update_irq_overrun(s);
}


static void g233_spi_write_cr2(G233SPIState *s, uint32_t value)
{
    uint32_t old_cs = s->cr2 & SPI_CR2_CS_SEL_MASK;
    uint32_t new_cs = value & SPI_CR2_CS_SEL_MASK;

    if (new_cs != old_cs &&
        ((s->flag >> old_cs) & 0x01) &&
        s->channels[old_cs]) {
        g233_flash_xfer_cs_down(s->channels[old_cs]);
    }

    s->cr2 = new_cs;
}


static void g233_spi_write_sr(G233SPIState *s, uint32_t value)
{
    if (value & SPI_SR_OVERRUN) {
        s->sr &= ~SPI_SR_OVERRUN;
    }
}

static void g233_spi_write_dr(G233SPIState *s, uint32_t value)
{

    uint8_t tx = value & 0xFF;
    uint8_t rx;

    if (s->sr & SPI_SR_RXNE) {
        /* 上一字节还没被读走，产生 overrun */
        s->sr |= SPI_SR_OVERRUN;
        g233_spi_update_irq_overrun(s);
        return;
    }

    if (((s->flag >> s->cr2) & 0x01) && s->channels[s->cr2]) {
        rx = g233_flash_xfer_byte(s->channels[s->cr2], tx);
    } else {
        /* 无 flash 时回环 */
        rx = tx;
    }

    s->dr = rx;
    s->sr |= SPI_SR_RXNE;
    s->sr |= SPI_SR_TXE;
    g233_spi_update_irq_rxne(s);
    g233_spi_update_irq_txe(s);
}
static uint64_t g233_spi_read_dr(G233SPIState *s){
    uint8_t ret = s->dr & 0xff;

    s->sr |= SPI_SR_TXE;
    s->sr &= ~SPI_SR_RXNE;
    g233_spi_update_irq_txe(s);
    g233_spi_update_irq_rxne(s);

    return ret;
}
static uint64_t g233_spi_read(void *opaque, hwaddr offset, unsigned size)
{
    G233SPIState *s = opaque;

    (void)size;

    switch (offset) {
    case SPI_CR1_OFF:
        return s->cr1 & SPI_CR1_WR_MASK;
    case SPI_CR2_OFF:
        return s->cr2 & SPI_CR2_CS_SEL_MASK;
    case SPI_SR_OFF:
        return s->sr;
    case SPI_DR_OFF:
        return g233_spi_read_dr(s);
    default:
        return 0;
    }
}

static void g233_spi_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    G233SPIState *s = opaque;

    (void)size;

    switch (offset) {
    case SPI_CR1_OFF:
        g233_spi_write_cr1(s, value);
        return;
    case SPI_CR2_OFF:
        g233_spi_write_cr2(s, value);
        return;
    case SPI_SR_OFF:
        g233_spi_write_sr(s, value);
        return;
    case SPI_DR_OFF:
        g233_spi_write_dr(s, value);
        return;
    default:
        return;
    }
}

static const MemoryRegionOps g233_spi_ops = {
    .read = g233_spi_read,
    .write = g233_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void g233_spi_reset(DeviceState *dev)
{
    G233SPIState *s = G233_SPI(dev);

    s->cr1 = 0;
    s->cr2 = 0;
    s->sr = SPI_SR_TXE;
    s->dr = 0;
    qemu_set_irq(s->irq, 0);
}

static void g233_spi_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    G233SPIState *s = G233_SPI(obj);

    memory_region_init_io(&s->mmio, obj, &g233_spi_ops, s, TYPE_G233_SPI, 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);

    for(uint32_t i=0;i<4;i++){
        s->channels[i] = NULL;
    }
    s->flag = 0;
}

static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "GEVICO G233 SPI";
    device_class_set_legacy_reset(dc, g233_spi_reset);
}

static const TypeInfo g233_spi_info = {
    .name = TYPE_G233_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233SPIState),
    .instance_init = g233_spi_init,
    .class_init = g233_spi_class_init,
};

static void g233_spi_register_types(void)
{
    type_register_static(&g233_spi_info);
}

type_init(g233_spi_register_types);
