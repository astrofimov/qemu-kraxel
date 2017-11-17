#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/loader.h"
#include "hw/isa/isa.h"
#include "hw/display/ramfb.h"
#include "ui/console.h"
#include "sysemu/sysemu.h"

#define TYPE_RAMFB "ramfb-testdev"
#define RAMFB(obj) OBJECT_CHECK(ISARAMFBState, (obj), TYPE_RAMFB)

typedef struct ISARAMFBState {
    ISADevice parent_obj;
    QemuConsole *con;
    RAMFBState *state;
    PortioList vga_port_list;
} ISARAMFBState;

/* log vga port activity, for trouble-shooting purposes */
static uint32_t vga_log_read(void *opaque, uint32_t addr)
{
    fprintf(stderr, "%s: port 0x%x\n", __func__, addr);
    return -1;
}

static void vga_log_write(void *opaque, uint32_t addr, uint32_t val)
{
    fprintf(stderr, "%s: port 0x%x, value 0x%x\n", __func__, addr, val);
}

static const MemoryRegionPortio vga_portio_list[] = {
    { 0x04,  2, 1, .read = vga_log_read, .write = vga_log_write }, /* 3b4 */
    { 0x0a,  1, 1, .read = vga_log_read, .write = vga_log_write }, /* 3ba */
    { 0x10, 16, 1, .read = vga_log_read, .write = vga_log_write }, /* 3c0 */
    { 0x24,  2, 1, .read = vga_log_read, .write = vga_log_write }, /* 3d4 */
    { 0x2a,  1, 1, .read = vga_log_read, .write = vga_log_write }, /* 3da */
    PORTIO_END_OF_LIST(),
};

static void display_update_wrapper(void *dev)
{
    ISARAMFBState *ramfb = RAMFB(dev);

    if (0 /* native driver active */) {
        /* non-test device would run native display update here */;
    } else {
        ramfb_display_update(ramfb->con, ramfb->state);
    }
}

static const GraphicHwOps wrapper_ops = {
    .gfx_update = display_update_wrapper,
};

static void ramfb_realizefn(DeviceState *dev, Error **errp)
{
    ISARAMFBState *ramfb = RAMFB(dev);

    ramfb->con = graphic_console_init(dev, 0, &wrapper_ops, dev);
    ramfb->state = ramfb_setup(errp);

    isa_register_portio_list(ISA_DEVICE(dev), &ramfb->vga_port_list,
                             0x3b0, vga_portio_list, NULL, "vga");
}

static void ramfb_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->realize = ramfb_realizefn;
    dc->desc = "ram framebuffer test device";
}

static const TypeInfo ramfb_info = {
    .name          = TYPE_RAMFB,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISARAMFBState),
    .class_init    = ramfb_class_initfn,
};

static void ramfb_register_types(void)
{
    type_register_static(&ramfb_info);
}

type_init(ramfb_register_types)
