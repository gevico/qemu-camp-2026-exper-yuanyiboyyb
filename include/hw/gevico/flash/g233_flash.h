#ifndef HW_GEVICO_FLASH_G233_FLASH_H
#define HW_GEVICO_FLASH_G233_FLASH_H

#include "qom/object.h"
#include "hw/core/qdev.h"




#define TYPE_G233_FLASH "gevico.g233-flash"
OBJECT_DECLARE_SIMPLE_TYPE(G233FLASHState, G233_FLASH)

enum {
    G233_FLASH_CMD_JEDEC_ID = 0x9F,
    G233_FLASH_CMD_RDSR     = 0x05,
    G233_FLASH_CMD_WREN     = 0x06,
    G233_FLASH_CMD_WRDI     = 0x04,
    G233_FLASH_CMD_SE       = 0x20,
    G233_FLASH_CMD_PP       = 0x02,
    G233_FLASH_CMD_READ     = 0x03,
};

enum{
    G233_FLASH_CS_UP,
    G233_FLASH_CS_DOWN
};

enum {
    G233_FLASH_SR_WIP = (1U << 0),
    G233_FLASH_SR_WEL = (1U << 1),
};

typedef enum G233FlashXferState {
    G233_FLASH_XFER_IDLE = 0,
    G233_FLASH_XFER_JEDEC_ID_B0,
    G233_FLASH_XFER_JEDEC_ID_B1,
    G233_FLASH_XFER_JEDEC_ID_B2,
    G233_FLASH_XFER_RDSR,
    G233_FLASH_XFER_READ_ADDR_B0,
    G233_FLASH_XFER_READ_ADDR_B1,
    G233_FLASH_XFER_READ_ADDR_B2,
    G233_FLASH_XFER_READ_STREAM,
    G233_FLASH_XFER_PP_ADDR_B0,
    G233_FLASH_XFER_PP_ADDR_B1,
    G233_FLASH_XFER_PP_ADDR_B2,
    G233_FLASH_XFER_PP_STREAM,
    G233_FLASH_XFER_SE_ADDR_B0,
    G233_FLASH_XFER_SE_ADDR_B1,
    G233_FLASH_XFER_SE_ADDR_B2,
} G233FlashXferState;

struct G233FLASHState {
    DeviceState parent_obj;
    G233FlashXferState xfer_state;
    uint8_t status;
    uint32_t size;
    uint32_t op_addr;
    uint32_t addr_latch;
    uint8_t *store;
    uint32_t jedec_id;
    uint32_t pp_page_end;
};

void g233_flash_xfer_cs_down(G233FLASHState *s);
uint8_t g233_flash_xfer_byte(G233FLASHState *s, uint8_t tx);

#endif 
