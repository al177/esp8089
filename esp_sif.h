/*
 * Copyright (c) 2011 - 2013 Espressif System.
 *
 *   Serial I/F wrapper layer for eagle WLAN device,
 *    abstraction of buses like SDIO/SIP, and provides
 *    flow control for tx/rx layer
 *
 */

#ifndef _ESP_SIF_H_
#define _ESP_SIF_H_

#include "esp_pub.h"
#include <linux/mmc/host.h>

/*
 *  H/W SLC module definitions
 */

#define SIF_SLC_BLOCK_SIZE                512

#define SIF_DMA_BUFFER_SIZE (64 * 1024)

/* to make the last byte located at :xffff, increase 1 byte here */
//#define SIF_SLC_WINDOW_END_ADDR  (0xffff + 1)
//#define SIF_SLC_WINDOW_END_ADDR  (0x1ffff + 1 - 0x800)

#define SIF_MAX_SCATTER_REQUESTS             4
#define SIF_MAX_SCATTER_ENTRIES_PER_REQ      16
#define SIF_MAX_SCATTER_REQ_TRANSFER_SIZE    (32 * 1024)


/* SIF bus request */
#define SIF_REQ_MAX_NUM                64

/* S/W struct mapping to slc registers */
typedef struct slc_host_regs {
        /* do NOT read token_rdata
         *
                u32 pf_data;
                u32 token_rdata;
        */
        u32 intr_raw;
        u32 state_w0;
        u32 state_w1;
        u32 config_w0;
        u32 config_w1;
        u32 intr_status;
        u32 config_w2;
        u32 config_w3;
        u32 config_w4;
        u32 token_wdata;
        u32 intr_clear;
        u32 intr_enable;
} sif_slc_reg_t;


struct sif_req {
        struct list_head list;

        u32 address;

        u8 *buffer;
        u32 length;
        u32 flag;
        int status;
        void * context;
};

#define SIF_TO_DEVICE                    0x1
#define SIF_FROM_DEVICE                    0x2

#define SIF_SYNC             0x00000010
#define SIF_ASYNC           0x00000020

#define SIF_BYTE_BASIS              0x00000040
#define SIF_BLOCK_BASIS             0x00000080

#define SIF_FIXED_ADDR           0x00000100
#define SIF_INC_ADDR     0x00000200


static void inline sif_setup_req(struct sif_req *req, u32 addr, u32 flag, u32 len,
                                 u8 * buf, void *context)
{
        req->address = addr;
        req->flag = flag;
        req->length = len;
        req->buffer = buf;
        req->context = context;
}

void sdio_io_writeb(struct esp_pub *epub, u8 value, int addr, int *res);
u8 sdio_io_readb(struct esp_pub *epub, int addr, int *res);


void sif_enable_irq(struct esp_pub *epub);
void sif_disable_irq(struct esp_pub *epub);
void sif_disable_target_interrupt(struct esp_pub *epub);

int sif_io_raw(struct esp_pub *epub, u32 addr, u8 *buf, u32 len, u32 flag);
int sif_io_sync(struct esp_pub *epub, u32 addr, u8 *buf, u32 len, u32 flag);
int sif_io_async(struct esp_pub *epub, u32 addr, u8 *buf, u32 len, u32 flag, void * context);
u32 sif_get_blksz(struct esp_pub *epub);
u32 sif_get_target_id(struct esp_pub *epub);
int sif_lldesc_read_sync(struct esp_pub *epub, u8 *buf, u32 len);
int sif_lldesc_write_sync(struct esp_pub *epub, u8 *buf, u32 len);
int sif_lldesc_read_raw(struct esp_pub *epub, u8 *buf, u32 len, bool noround);
int sif_lldesc_write_raw(struct esp_pub *epub, u8 *buf, u32 len);


struct slc_host_regs * sif_get_regs(struct esp_pub *epub);

void sif_lock_bus(struct esp_pub *epub);
void sif_unlock_bus(struct esp_pub *epub);

void sif_platform_target_poweroff(void);
void sif_platform_target_poweron(void);
void sif_platform_target_speed(int high_speed);

void sif_platform_reset_target(void);
void sif_platform_rescan_card(unsigned insert);

void sif_raw_dummy_read(struct esp_pub *epub);

void sif_record_bt_config(int value);
int sif_get_bt_config(void);
void sif_record_rst_config(int value);
int sif_get_rst_config(void);
void sif_record_retry_config(void);
int sif_get_retry_config(void);

#ifdef ESP_ACK_INTERRUPT
//extern void sif_platform_ack_interrupt(struct mmc_host *mmc);
extern void sif_platform_ack_interrupt(struct esp_pub *epub);
#endif //ESP_ACK_INTERRUPT

#define sif_reg_read_sync(epub, addr, buf, len) sif_io_sync((epub), (addr), (buf), (len), SIF_FROM_DEVICE | SIF_BYTE_BASIS | SIF_INC_ADDR)

#define sif_reg_write_sync(epub, addr, buf, len) sif_io_sync((epub), (addr), (buf), (len), SIF_TO_DEVICE | SIF_BYTE_BASIS | SIF_INC_ADDR)

#endif /* _ESP_SIF_H_ */
