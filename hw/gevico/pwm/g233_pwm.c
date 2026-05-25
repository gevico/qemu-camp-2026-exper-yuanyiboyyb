#include "qemu/osdep.h"
#include "hw/gevico/pwm/g233_pwm.h"
#include "hw/core/irq.h"
#include "system/runstate.h"

#define PWM_GLB_OFF         0x00
#define PWM_CTRL_OFF        0x00
#define PWM_PERIOD_OFF      0x04
#define PWM_DUTY_OFF        0x08
#define PWM_CNT_OFF         0x0C

#define PWM_CTRL_INTIE      BIT(2)
#define PWM_CTRL_POL        BIT(1)
#define PWM_CTRL_EN         BIT(0)

#define PWM_TICK_NS         1000LL

static void g233_pwm_write_channel_ctrl(G233PWMState*s,G233PWMChannel* channel,uint32_t value){
    uint32_t turn_on =  ~(channel->ctrl) & value &  PWM_CTRL_EN;
    uint32_t turn_off = channel->ctrl & (~value) & PWM_CTRL_EN;
    channel->ctrl = value;
    if (channel->ctrl & PWM_CTRL_EN) {
        s->glb |= BIT(channel->index);
    } else {
        s->glb &= ~BIT(channel->index);
    }
    if(value&PWM_CTRL_INTIE){
        qemu_set_irq(s->irq, 0);
    }
    if(turn_on){
        int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        int64_t delta_ns = (int64_t)channel->preiod * PWM_TICK_NS;
        channel->expire_ns = now_ns + delta_ns;
        timer_mod(channel->timer,channel->expire_ns);
    }else if(turn_off){
        timer_del(channel->timer);
    }
}
static void g233_pwm_write_channel(G233PWMState*s,uint32_t aim,uint32_t off,uint32_t value){
    switch (off){
        case PWM_CTRL_OFF:
            g233_pwm_write_channel_ctrl(s,&s->channels[aim],value);
            return;
        case PWM_PERIOD_OFF:
            s->channels[aim].preiod = value;
            return;
        case PWM_DUTY_OFF:
            s->channels[aim].duty = value;
            return;
        case PWM_CNT_OFF:
            return;
        default:
            return;
    }
}
static void g233_pwm_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    G233PWMState *s = opaque;
    (void)size;

    switch (offset) {
        case PWM_GLB_OFF:
            s->glb &= ~(value & 0xF0);
            if(!(s->glb&0xF0)){
                qemu_set_irq(s->irq, 0);
            }
            return;
        default:
            uint32_t aim = ((offset & 0xF0)>>4)-1;
            uint32_t off = offset & 0x0F;
            if(aim<=5){
                g233_pwm_write_channel(s,aim,off,value);
            }
            return;
    }

}
static uint64_t g233_pwm_read_channel_cnt(G233PWMChannel* channel){
    int64_t now_ns =  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t remain_ns = channel->expire_ns - now_ns;
    if (remain_ns <= 0) {
        return 0;
    }
    return (uint32_t)DIV_ROUND_UP(remain_ns, PWM_TICK_NS);
}
static uint64_t g233_pwm_read_channel(G233PWMState*s,uint32_t aim,uint32_t off){
    switch (off){
        case PWM_CTRL_OFF:
            return s->channels[aim].ctrl;
        case PWM_PERIOD_OFF:
            return s->channels[aim].preiod;
        case PWM_DUTY_OFF:
            return s->channels[aim].duty;
        case PWM_CNT_OFF:
            return g233_pwm_read_channel_cnt(&s->channels[aim]);
        default:
            return 0;
    }
}
static uint64_t g233_pwm_read(void *opaque, hwaddr offset, unsigned size)
{
    G233PWMState *s = opaque;
    (void)size;

    switch (offset) {
        case PWM_GLB_OFF:
            return s->glb;
        default:
            uint32_t aim = ((offset & 0xF0)>>4)-1;
            uint32_t off = offset & 0x0F;
            if(off<=0x50){
                return g233_pwm_read_channel(s,aim,off);
            }
            return 0;
    }
}
static const MemoryRegionOps g233_pwm_ops = {
    .read = g233_pwm_read,
    .write = g233_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};
static void g233_pwm_timeout(void *opaque)
{
    G233PWMChannel *channel = opaque;
    G233PWMState *s = container_of(channel,G233PWMState,channels[channel->index]);

    s->glb |= BIT(channel->index+4);

    if (channel->ctrl & PWM_CTRL_INTIE) {
        qemu_set_irq(s->irq, 1);
    }
    if(channel->ctrl & PWM_CTRL_EN){
        int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        int64_t delta_ns = (int64_t)channel->preiod * PWM_TICK_NS;
        channel->expire_ns = now_ns + delta_ns;
        timer_mod(channel->timer,channel->expire_ns);
    }
}
static void g233_pwm_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    G233PWMState *s = G233_PWM(obj);

    sysbus_init_mmio(sbd, &s->mmio);
    memory_region_init_io(&s->mmio, obj, &g233_pwm_ops, s, TYPE_G233_PWM, 0x1000);
    sysbus_init_irq(sbd, &s->irq);
    
    for(int i=0;i<4;i++){
         s->channels[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, g233_pwm_timeout, &s->channels[i]);
    }
}
static void g233_pwm_reset(DeviceState *dev)
{
    G233PWMState *s = G233_PWM(dev);

    s->glb = 0;
    qemu_set_irq(s->irq, 0);

    for(int i=0;i<4;i++){
        s->channels[i].index = i;
        s->channels[i].ctrl = 0;
        s->channels[i].preiod = 0;
        s->channels[i].duty = 0;
        s->channels[i].cnt = 0;
        s->channels[i].expire_ns = 0;
        timer_del(s->channels[i].timer);
    }
}
static void g233_pwm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "GEVICO G233 PWM";
    device_class_set_legacy_reset(dc, g233_pwm_reset);
}
static const TypeInfo g233_pwm_info = {
    .name = TYPE_G233_PWM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233PWMState),
    .instance_init = g233_pwm_init,
    .class_init = g233_pwm_class_init,
};

static void g233_pwm_register_types(void)
{
    type_register_static(&g233_pwm_info);
}

type_init(g233_pwm_register_types);
