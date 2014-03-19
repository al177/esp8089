/*
 * Copyright (c) 2010 -2013 Espressif System.
 *
 *   sdio serial i/f driver
 *    - sdio device control routines
 *    - sync/async DMA/PIO read/write
 *
 */

#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sd.h>
#include <linux/module.h>
#include <net/mac80211.h>
#include <linux/time.h>
#include <linux/pm.h>

#include "esp_pub.h"
#include "esp_sif.h"
#include "esp_sip.h"
#include "esp_debug.h"
#include "slc_host_register.h"
#include "esp_version.h"
#include "esp_ctrl.h"
#ifdef ANDROID
#include "esp_android.h"
#endif /* ANDROID */

#define CCCR_SDIO_IRQ_MODE_REG         0xF0
#define SDIO_IRQ_MODE_ASYNC_4BIT_IRQ   (1 << 0)
#define CMD53_FIXED_ADDRESS 1
#define CMD53_INCR_ADDRESS  2

#define ESP_DMA_IBUFSZ   2048

//unsigned int esp_msg_level = 0;
unsigned int esp_msg_level = ESP_DBG_ERROR | ESP_SHOW ; 

static struct semaphore esp_powerup_sem;

static enum esp_sdio_state sif_sdio_state;
struct esp_sdio_ctrl *sif_sctrl = NULL;

#ifdef ESP_ANDROID_LOGGER
bool log_off = false;
#endif /* ESP_ANDROID_LOGGER */

typedef struct esp_sdio_ctrl {
        struct sdio_func *func;
        struct esp_pub *epub;


        struct list_head free_req;
        struct sif_req reqs[SIF_REQ_MAX_NUM];

        u8 *dma_buffer;

        spinlock_t scat_lock;
        struct list_head scat_req;

        bool off;
        atomic_t irq_handling;
        const struct sdio_device_id *id;
        u32 slc_blk_sz;
        u32 target_id;
        u32 slc_window_end_addr;

        struct slc_host_regs slc_regs;
        atomic_t 	irq_installed;

} esp_sdio_ctrl_t;

static int esdio_power_off(struct esp_sdio_ctrl *sctrl);
static int esdio_power_on(struct esp_sdio_ctrl *sctrl);

void sif_set_clock(struct sdio_func *func, int clk);

#ifdef SIF_DEBUG_DSR_DUMP_REG
static void dump_slc_regs(struct slc_host_regs *regs);
#endif /* SIF_DEBUG_DSR_DUMP_REG */

struct sif_req * sif_alloc_req(struct esp_sdio_ctrl *sctrl);

#if 1
static inline void set_cmd53_arg(u32 *arg, u8 rw, u8 func,
                                 u8 mode, u8 opcode, u32 addr,
                                 u16 blksz)
{
        *arg = (((rw & 1) << 31) |
                ((func & 0x7) << 28) |
                ((mode & 1) << 27) |
                ((opcode & 1) << 26) |
                ((addr & 0x1FFFF) << 9) |
                (blksz & 0x1FF));
}

static inline void set_cmd52_arg(u32 *arg, u8 write, u8 raw,
                                 unsigned int address,
                                 unsigned char val)
{
        const u8 func = 0;

        *arg = ((write & 1) << 31) |
               ((func & 0x7) << 28) |
               ((raw & 1) << 27) |
               (1 << 26) |
               ((address & 0x1FFFF) << 9) |
               (1 << 8) |
               (val & 0xFF);
}

#if 0
static int func0_cmd52_wr_byte(struct mmc_card *card,
                               unsigned int address,
                               unsigned char byte)
{
        struct mmc_command io_cmd;

        memset(&io_cmd, 0, sizeof(io_cmd));
        set_cmd52_arg(&io_cmd.arg, 1, 0, address, byte);
        io_cmd.opcode = SD_IO_RW_DIRECT;
        io_cmd.flags = MMC_RSP_R5 | MMC_CMD_AC;

        return mmc_wait_for_cmd(card->host, &io_cmd, 0);
}

static int func0_cmd52_rd_byte(struct mmc_card *card,
                               unsigned int address,
                               unsigned char *byte)
{
        struct mmc_command io_cmd;
        int err = 0;

        memset(&io_cmd, 0, sizeof(io_cmd));
        set_cmd52_arg(&io_cmd.arg, 0, 0, address, 0);
        io_cmd.opcode = SD_IO_RW_DIRECT;
        io_cmd.flags = MMC_RSP_R5 | MMC_CMD_AC;

        err = mmc_wait_for_cmd(card->host, &io_cmd, 0);

        if (!err && byte)
                *byte = io_cmd.resp[0] & 0xff;

        return err;
}
#endif /*0000*/

/*
 * Bypass linux sdio layer to do raw sdio cmds
 */
#if 0
static int emmc_io_rw_direct(struct mmc_card *card, int write, unsigned fn,
                             unsigned addr, u8 in, u8* out)
{
        struct mmc_command cmd;
        int err;

        BUG_ON(!card);
        BUG_ON(fn > 7);

        /* sanity check */
        if (addr & ~0x1FFFF)
                return -EINVAL;

        memset(&cmd, 0, sizeof(struct mmc_command));

        cmd.opcode = SD_IO_RW_DIRECT;
        cmd.arg = write ? 0x80000000 : 0x00000000;
        cmd.arg |= fn << 28;
        cmd.arg |= (write && out) ? 0x08000000 : 0x00000000;
        cmd.arg |= addr << 9;
        cmd.arg |= in;
        cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_AC;

        err = mmc_wait_for_cmd(card->host, &cmd, 0);

        if (err)
                return err;

        if (cmd.resp[0] & R5_ERROR)
                return -EIO;

        if (cmd.resp[0] & R5_FUNCTION_NUMBER)
                return -EINVAL;

        if (cmd.resp[0] & R5_OUT_OF_RANGE)
                return -ERANGE;

        if (out) {
                if (mmc_host_is_spi(card->host))
                        *out = (cmd.resp[0] >> 8) & 0xFF;
                else
                        *out = cmd.resp[0] & 0xFF;
        }

        return 0;
}
#endif /* 0000 */
#endif /* 1111 */

#ifdef USE_OOB_INTR
typedef enum _SDIO_INTR_MODE {
	SDIO_INTR_IB = 0,
	SDIO_INTR_OOB_TOGGLE,
	SDIO_INTR_OOB_HIGH_LEVEL,
	SDIO_INTR_OOB_LOW_LEVEL,
} SDIO_INTR_MODE;

#define GEN_GPIO_SEL(_gpio_num, _sel_func, _intr_mode, _offset) (((_offset)<< 9 ) |((_intr_mode) << 7)|((_sel_func) << 4)|(_gpio_num))
//bit[3:0] = gpio num, 2
//bit[6:4] = gpio sel func, 0
//bit[8:7] = gpio intr mode, SDIO_INTR_OOB_TOGGLE
//bit[15:9] = register offset, 0x38

u16 gpio_sel_sets[17] = {
	GEN_GPIO_SEL(0, 0, SDIO_INTR_OOB_TOGGLE, 0x34),//GPIO0
	GEN_GPIO_SEL(1, 3, SDIO_INTR_OOB_TOGGLE, 0x18),//U0TXD
	GEN_GPIO_SEL(2, 0, SDIO_INTR_OOB_TOGGLE, 0x38),//GPIO2
	GEN_GPIO_SEL(3, 3, SDIO_INTR_OOB_TOGGLE, 0x14),//U0RXD
	GEN_GPIO_SEL(4, 0, SDIO_INTR_OOB_TOGGLE, 0x3C),//GPIO4
	GEN_GPIO_SEL(5, 0, SDIO_INTR_OOB_TOGGLE, 0x40),//GPIO5
	GEN_GPIO_SEL(6, 3, SDIO_INTR_OOB_TOGGLE, 0x1C),//SD_CLK
	GEN_GPIO_SEL(7, 3, SDIO_INTR_OOB_TOGGLE, 0x20),//SD_DATA0
	GEN_GPIO_SEL(8, 3, SDIO_INTR_OOB_TOGGLE, 0x24),//SD_DATA1
	GEN_GPIO_SEL(9, 3, SDIO_INTR_OOB_TOGGLE, 0x28),//SD_DATA2
	GEN_GPIO_SEL(10, 3, SDIO_INTR_OOB_TOGGLE, 0x2C),//SD_DATA3
	GEN_GPIO_SEL(11, 3, SDIO_INTR_OOB_TOGGLE, 0x30),//SD_CMD
	GEN_GPIO_SEL(12, 3, SDIO_INTR_OOB_TOGGLE, 0x04),//MTDI
	GEN_GPIO_SEL(13, 3, SDIO_INTR_OOB_TOGGLE, 0x08),//MTCK
	GEN_GPIO_SEL(14, 3, SDIO_INTR_OOB_TOGGLE, 0x0C),//MTMS
	GEN_GPIO_SEL(15, 3, SDIO_INTR_OOB_TOGGLE, 0x10),//MTDO
	//pls do not change sel before, if you want to change intr mode,change the one blow
	//GEN_GPIO_SEL(2, 0, SDIO_INTR_OOB_TOGGLE, 0x38)
	GEN_GPIO_SEL(2, 0, SDIO_INTR_OOB_LOW_LEVEL, 0x38)
};
#endif

void sif_raw_dummy_read(struct esp_pub *epub)
{
        struct esp_sdio_ctrl *sctrl = NULL;
        struct sdio_func *func = NULL;
//        int err = 0;
	int retry = 0;
//        int i;
//        u32 v = 0;
        u32 *p_target_id = NULL;
	static u32 read_err_cnt = 0;
	static u32 write_err_cnt = 0;
	static u32 unknow_err_cnt = 0;
	static u32 check_cnt = 0;
        
        ASSERT(epub != NULL);

        sctrl = (struct esp_sdio_ctrl *)epub->sif;
        func = sctrl->func;

	p_target_id = kzalloc(sizeof(u32), GFP_KERNEL);
	ASSERT(p_target_id != NULL);
	*p_target_id = 0;
		
	*p_target_id = 0x010001ff;
	sdio_memcpy_toio(func, SLC_HOST_CONF_W4, p_target_id, sizeof(u32));

        do {
#if 0
                for (i = 0; i < 4; i++) {
                        err = func0_cmd52_rd_byte(func->card, SLC_HOST_ID+i, ((u8 *)&v + i));
                }

                if (v == sctrl->target_id) {
                        //printk("%s err %d v %x counter %d\n", __func__, err, v, 6 - retry);
                        break;
                }
#endif
		*p_target_id = 0xffffffff;
		udelay(20);
		sdio_memcpy_fromio(func, p_target_id, SLC_HOST_CONF_W4, sizeof(u32));

		if(*p_target_id == 0x020001ff)
			break;
		
		if(*p_target_id == 0x010001ff){
			if(retry < 5)
				continue;
		}else if(*p_target_id == 0x000001ff){
			write_err_cnt++;
		}else if(*p_target_id == 0xffffffff){
			read_err_cnt++;
			write_err_cnt++;
		}else
			unknow_err_cnt++;

		*p_target_id = 0x010001ff;
		udelay(20);
		sdio_memcpy_toio(func, SLC_HOST_CONF_W4, p_target_id, sizeof(u32));
                //sdio_memcpy_fromio(func, p_target_id, SLC_HOST_ID, sizeof(u32));
                
                //if (*p_target_id == sctrl->target_id)
                        //break;

        } while (retry++ < 100);
	
	kfree(p_target_id);

	if(read_err_cnt || write_err_cnt || unknow_err_cnt){
		if((check_cnt & 0xf) == 0)
			printk("==============sdio err===============\n,read:%u, write:%u, unknow:%u\n", read_err_cnt,write_err_cnt,unknow_err_cnt);
		check_cnt++;
	}

        if (retry > 1) {
                printk("=========%s tried %d times===========\n", __func__, retry - 1);
                //if (retry>=100)
                //        ASSERT(0);
        }
#if 0
        if (err)
                printk("=========%s error %d after %d times read!!===============\n", __func__, err, retry); 
#endif
}

static void check_target_id(struct sdio_func *func)
{
        struct esp_sdio_ctrl *sctrl = sdio_get_drvdata(func);
        u32 date;
        int err = 0;
        int i;

        sdio_claim_host(func);

        //err = sdio_memcpy_fromio(func, &date, SLC_HOST_DATE, sizeof(u32));

        //err = sdio_memcpy_fromio(func, &(sctrl->target_id), SLC_HOST_ID, sizeof(u32));

        for(i = 0; i < 4; i++) {
                *((u8 *)&date + i) = sdio_readb(func, SLC_HOST_DATE + i, &err);
                *((u8 *)&sctrl->target_id + i) = sdio_readb(func, SLC_HOST_ID + i, &err);
        }
        sdio_release_host(func);

        esp_dbg(ESP_DBG_LOG, "\n\n \t\t SLC data 0x%08x, ID 0x%08x\n\n", date, sctrl->target_id);

        switch(sctrl->target_id) {
        case 0x100:
                sctrl->slc_window_end_addr = 0x20000;
                break;
        case 0x600:
                sctrl->slc_window_end_addr = 0x20000 - 0x800;
#ifdef USE_OOB_INTR
		do{
			u16 gpio_sel = gpio_sel_sets[16];
			u8 low_byte = gpio_sel;
			u8 high_byte = gpio_sel >> 8;
			sdio_claim_host(func);
			sdio_writeb(func, low_byte, SLC_HOST_CONF_W5, &err);
			sdio_writeb(func, high_byte, SLC_HOST_CONF_W5 + 1, &err);
			sdio_release_host(func);
		}while(0);
#endif
                break;
        default:
                sctrl->slc_window_end_addr = 0x20000;
                break;
        }
}

static inline bool bad_buf(u8 * buf)
{
        return ((unsigned long) buf & 0x3) || !virt_addr_valid(buf);
}

void sif_lock_bus(struct esp_pub *epub)
{
        struct esp_sdio_ctrl *sctrl = NULL;
        struct sdio_func *func = NULL;

        ASSERT(epub != NULL);
        sctrl = (struct esp_sdio_ctrl *)epub->sif;
        func = sctrl->func;
        ASSERT(func != NULL);

        sdio_claim_host(func);
}

void sif_unlock_bus(struct esp_pub *epub)
{
        struct esp_sdio_ctrl *sctrl = NULL;
        struct sdio_func *func = NULL;

        ASSERT(epub != NULL);
        sctrl = (struct esp_sdio_ctrl *)epub->sif;
        func = sctrl->func;
        ASSERT(func != NULL);

        sdio_release_host(func);
}

int sif_io_raw(struct esp_pub *epub, u32 addr, u8 *buf, u32 len, u32 flag)
{
        int err = 0;
        u8 *ibuf = NULL;
        bool need_ibuf = false;
        struct esp_sdio_ctrl *sctrl = NULL;
        struct sdio_func *func = NULL;

        ASSERT(epub != NULL);
        ASSERT(buf != NULL);

        sctrl = (struct esp_sdio_ctrl *)epub->sif;
        func = sctrl->func;
        ASSERT(func != NULL);

        if (bad_buf(buf)) {
                esp_dbg(ESP_DBG_TRACE, "%s dst 0x%08x, len %d badbuf\n", __func__, addr, len);
                need_ibuf = true;
                ibuf = sctrl->dma_buffer;
        } else {
                ibuf = buf;
        }

        if (flag & SIF_BLOCK_BASIS) {
                /* round up for block data transcation */
        }

        if (flag & SIF_TO_DEVICE) {

                if (need_ibuf)
                        memcpy(ibuf, buf, len);

                if (flag & SIF_FIXED_ADDR)
                        err = sdio_writesb(func, addr, ibuf, len);
                else if (flag & SIF_INC_ADDR) {
                        err = sdio_memcpy_toio(func, addr, ibuf, len);
                }
        } else if (flag & SIF_FROM_DEVICE) {

                if (flag & SIF_FIXED_ADDR)
                        err = sdio_readsb(func, ibuf, addr, len);
                else if (flag & SIF_INC_ADDR) {
                        err = sdio_memcpy_fromio(func, ibuf, addr, len);
                }


                if (!err && need_ibuf)
                        memcpy(buf, ibuf, len);
        }

        return err;
}

#ifdef SDIO_TEST
static void sif_test_tx(struct esp_sdio_ctrl *sctrl)
{
        int i, err = 0;

        for (i = 0; i < 500; i++) {
                sctrl->dma_buffer[i] = i;
        }

        sdio_claim_host(sctrl->func);
        err = sdio_memcpy_toio(sctrl->func, 0x10001 - 500, sctrl->dma_buffer, 500);
        sdio_release_host(sctrl->func);

        esp_dbg(ESP_DBG, "%s toio err %d\n", __func__, err);
}

static void sif_test_dsr(struct sdio_func *func)
{
        struct esp_sdio_ctrl *sctrl = sdio_get_drvdata(func);

        sdio_release_host(sctrl->func);

        /* no need to read out registers in normal operation any more */
        //sif_io_sync(sctrl->epub, SIF_SLC_WINDOW_END_ADDR - 64, sctrl->dma_buffer, 64, SIF_FROM_DEVICE | SIF_INC_ADDR | SIF_SYNC | SIF_BYTE_BASIS);
        //
        esp_dsr(sctrl->epub);

        sdio_claim_host(func);

        //show_buf(sctrl->dma_buffer, 64);
}

void sif_test_rx(struct esp_sdio_ctrl *sctrl)
{
        int err = 0;

        sdio_claim_host(sctrl->func);

        err = sdio_claim_irq(sctrl->func, sif_test_dsr);

        if (err)
                esp_dbg(ESP_DBG_ERROR, "sif %s failed\n", __func__);

        sdio_release_host(sctrl->func);
}
#endif //SDIO_TEST

void sdio_io_writeb(struct esp_pub *epub, u8 value, int addr, int *res)
{
        struct esp_sdio_ctrl *sctrl = NULL;
        struct sdio_func *func = NULL;
        sctrl = (struct esp_sdio_ctrl *)epub->sif;
        func = sctrl->func;

        sdio_f0_writeb(func, value, addr, res);
}

u8 sdio_io_readb(struct esp_pub *epub, int addr, int *res)
{
        struct esp_sdio_ctrl *sctrl = NULL;
        struct sdio_func *func = NULL;
        sctrl = (struct esp_sdio_ctrl *)epub->sif;
        func = sctrl->func;

        return sdio_f0_readb(func, addr, res);
}

int sif_io_sync(struct esp_pub *epub, u32 addr, u8 *buf, u32 len, u32 flag)
{
        int err = 0;
        u8 * ibuf = NULL;
        bool need_ibuf = false;
        struct esp_sdio_ctrl *sctrl = NULL;
        struct sdio_func *func = NULL;

        ASSERT(epub != NULL);
        ASSERT(buf != NULL);

        sctrl = (struct esp_sdio_ctrl *)epub->sif;
        func = sctrl->func;
        ASSERT(func != NULL);

        if (bad_buf(buf)) {
                esp_dbg(ESP_DBG_TRACE, "%s dst 0x%08x, len %d badbuf\n", __func__, addr, len);
                need_ibuf = true;
                ibuf = sctrl->dma_buffer;
        } else {
                ibuf = buf;
        }

        if (flag & SIF_BLOCK_BASIS) {
                /* round up for block data transcation */
        }

        if (flag & SIF_TO_DEVICE) {

                esp_dbg(ESP_DBG_TRACE, "%s to addr 0x%08x, len %d \n", __func__, addr, len);
                if (need_ibuf)
                        memcpy(ibuf, buf, len);

                sdio_claim_host(func);

                if (flag & SIF_FIXED_ADDR)
                        err = sdio_writesb(func, addr, ibuf, len);
                else if (flag & SIF_INC_ADDR) {
                        err = sdio_memcpy_toio(func, addr, ibuf, len);
                }

                sdio_release_host(func);
        } else if (flag & SIF_FROM_DEVICE) {

                esp_dbg(ESP_DBG_TRACE, "%s from addr 0x%08x, len %d \n", __func__, addr, len);

                sdio_claim_host(func);

                if (flag & SIF_FIXED_ADDR)
                        err = sdio_readsb(func, ibuf, addr, len);
                else if (flag & SIF_INC_ADDR) {
                        err = sdio_memcpy_fromio(func, ibuf, addr, len);
                }

                sdio_release_host(func);

                if (!err && need_ibuf)
                        memcpy(buf, ibuf, len);
        }

        return err;
}

u32 sif_get_blksz(struct esp_pub *epub)
{
        struct esp_sdio_ctrl *sctrl = NULL;

        ASSERT(epub != NULL);

        sctrl = (struct esp_sdio_ctrl *)epub->sif;

        return sctrl->slc_blk_sz;
}

u32 sif_get_target_id(struct esp_pub *epub)
{
        struct esp_sdio_ctrl *sctrl = NULL;

        ASSERT(epub != NULL);

        sctrl = (struct esp_sdio_ctrl *)epub->sif;

        return sctrl->target_id;
}

int sif_lldesc_read_sync(struct esp_pub *epub, u8 *buf, u32 len)
{
        struct esp_sdio_ctrl *sctrl = NULL;
        u32 read_len;

        ASSERT(epub != NULL);

        sctrl = (struct esp_sdio_ctrl *)epub->sif;

        switch(sctrl->target_id) {
        case 0x100:
                read_len = len;
                break;
        case 0x600:
                read_len = roundup(len, sctrl->slc_blk_sz);
                break;
        default:
                read_len = len;
                break;
        }

        return sif_io_sync((epub), (sctrl->slc_window_end_addr - 2 - (len)), (buf), (read_len), SIF_FROM_DEVICE | SIF_BYTE_BASIS | SIF_INC_ADDR);
}

int sif_lldesc_write_sync(struct esp_pub *epub, u8 *buf, u32 len)
{
        struct esp_sdio_ctrl *sctrl = NULL;
        u32 write_len;

        ASSERT(epub != NULL);

        sctrl = (struct esp_sdio_ctrl *)epub->sif;

        switch(sctrl->target_id) {
        case 0x100:
                write_len = len;
                break;
        case 0x600:
                write_len = roundup(len, sctrl->slc_blk_sz);
                break;
        default:
                write_len = len;
                break;
        }

        return sif_io_sync((epub), (sctrl->slc_window_end_addr - (len)), (buf), (write_len), SIF_TO_DEVICE | SIF_BYTE_BASIS | SIF_INC_ADDR);
}

int sif_lldesc_read_raw(struct esp_pub *epub, u8 *buf, u32 len, bool noround)
{
        struct esp_sdio_ctrl *sctrl = NULL;
        u32 read_len;

        ASSERT(epub != NULL);

        sctrl = (struct esp_sdio_ctrl *)epub->sif;

        switch(sctrl->target_id) {
        case 0x100:
                read_len = len;
                break;
        case 0x600:
		if(!noround)
                	read_len = roundup(len, sctrl->slc_blk_sz);
		else
			read_len = len;
                break;
        default:
                read_len = len;
                break;
        }

        return sif_io_raw((epub), (sctrl->slc_window_end_addr - 2 - (len)), (buf), (read_len), SIF_FROM_DEVICE | SIF_BYTE_BASIS | SIF_INC_ADDR);
}

int sif_lldesc_write_raw(struct esp_pub *epub, u8 *buf, u32 len)
{
        struct esp_sdio_ctrl *sctrl = NULL;
        u32 write_len;

        ASSERT(epub != NULL);

        sctrl = (struct esp_sdio_ctrl *)epub->sif;

        switch(sctrl->target_id) {
        case 0x100:
                write_len = len;
                break;
        case 0x600:
                write_len = roundup(len, sctrl->slc_blk_sz);
                break;
        default:
                write_len = len;
                break;
        }
        return sif_io_raw((epub), (sctrl->slc_window_end_addr - (len)), (buf), (write_len), SIF_TO_DEVICE | SIF_BYTE_BASIS | SIF_INC_ADDR);

}

#define MANUFACTURER_ID_EAGLE_BASE        0x1110
#define MANUFACTURER_ID_EAGLE_BASE_MASK     0xFF00
#define MANUFACTURER_CODE                  0x6666

static const struct sdio_device_id esp_sdio_devices[] = {
        {SDIO_DEVICE(MANUFACTURER_CODE, (MANUFACTURER_ID_EAGLE_BASE | 0x1))},
        {},
};

static int esdio_power_on(struct esp_sdio_ctrl *sctrl)
{
        int err = 0;

        assert(sctrl != NULL);

        if (sctrl->off == false)
                return err;

        sdio_claim_host(sctrl->func);
        err = sdio_enable_func(sctrl->func);

        if (err) {
                esp_dbg(ESP_DBG_ERROR, "Unable to enable sdio func: %d\n", err);
                sdio_release_host(sctrl->func);
                return err;
        }

        sdio_release_host(sctrl->func);

        /* ensure device is up */
        msleep(5);

        sctrl->off = false;

        return err;
}

static int esdio_power_off(struct esp_sdio_ctrl *sctrl)
{
        int err;

        if (sctrl->off)
                return 0;

        sdio_claim_host(sctrl->func);
        err = sdio_disable_func(sctrl->func);
        sdio_release_host(sctrl->func);

        if (err)
                return err;

        sctrl->off = true;

        return err;
}

#ifdef SIF_CHECK_FIRST_INTR
static bool first_intr_checked = false;
#endif //SIF_CHECK_FIRST_INTR
 
static void sif_dsr(struct sdio_func *func)
{
        struct esp_sdio_ctrl *sctrl = sdio_get_drvdata(func);
#ifdef SIF_DSR_WAR
        static int dsr_cnt = 0, real_intr_cnt = 0, bogus_intr_cnt = 0;
        struct slc_host_regs *regs = &(sctrl->slc_regs);
       esp_dbg(ESP_DBG_TRACE, " %s enter %d \n", __func__, dsr_cnt++);
#endif /* SIF_DSR_WAR */

        atomic_set(&sctrl->irq_handling, 1);

        sdio_release_host(sctrl->func);

#ifdef SIF_DSR_WAR
        do {
#ifdef SIF_CHECK_FIRST_INTR 
                if (likely(first_intr_checked)) {
                        esp_dsr(sctrl->epub);
                        break;
                } 
#endif //SIF_CHECK_FIRST_INTR
          
                memset(regs, 0xff, sizeof(struct slc_host_regs));
                sif_io_sync(sctrl->epub, REG_SLC_HOST_BASE + 8, (u8 *)regs, sizeof(struct slc_host_regs), SIF_FROM_DEVICE | SIF_INC_ADDR | SIF_SYNC | SIF_BYTE_BASIS);

                if (regs->intr_status & SLC_HOST_RX_ST) {
#ifdef SIF_CHECK_FIRST_INTR 
                	first_intr_checked = true;
#endif //SIF_CHECK_FIRST_INTR
                        esp_dbg(ESP_DBG_TRACE, "%s eal intr cnt: %d", __func__, ++real_intr_cnt);
        	
			esp_dsr(sctrl->epub);

                } else {
#ifdef ESP_ACK_INTERRUPT
			sif_lock_bus(sctrl->epub);
			sif_platform_ack_interrupt(sctrl->epub);
			sif_unlock_bus(sctrl->epub);
#endif //ESP_ACK_INTERRUPT

                        esp_dbg(ESP_DBG_TRACE, "%s bogus_intr_cnt %d\n", __func__, ++bogus_intr_cnt);
                }

#ifdef SIF_DEBUG_DSR_DUMP_REG
                dump_slc_regs(regs);
#endif /* SIF_DEBUG_DUMP_DSR */

        } while (0);

#else
       	esp_dsr(sctrl->epub);
#endif /* SIF_DSR_WAR */

        sdio_claim_host(func);

        atomic_set(&sctrl->irq_handling, 0);
}

struct slc_host_regs * sif_get_regs(struct esp_pub *epub) 
{
        struct slc_host_regs * regs;
        struct esp_sdio_ctrl *sctrl = (struct esp_sdio_ctrl *)epub->sif;

        regs = &(sctrl->slc_regs);

        return regs;
}

void sif_enable_irq(struct esp_pub *epub) 
{
        int err;
        struct esp_sdio_ctrl *sctrl = NULL;

        sctrl = (struct esp_sdio_ctrl *)epub->sif;

        sdio_claim_host(sctrl->func);

        err = sdio_claim_irq(sctrl->func, sif_dsr);

        if (err)
                esp_dbg(ESP_DBG_ERROR, "sif %s failed\n", __func__);

        atomic_set(&epub->sip->state, SIP_BOOT);

        atomic_set(&sctrl->irq_installed, 1);

        sdio_release_host(sctrl->func);
}

void sif_disable_target_interrupt(struct esp_pub *epub)
{
	struct esp_sdio_ctrl *sctrl = (struct esp_sdio_ctrl *)epub->sif;

	sdio_claim_host(sctrl->func);
#ifdef HOST_RESET_BUG
	mdelay(10);
#endif
	memset(sctrl->dma_buffer, 0x00, sizeof(u32));
	sdio_memcpy_toio(sctrl->func, SLC_HOST_INT_ENA, sctrl->dma_buffer, sizeof(u32));

#ifdef HOST_RESET_BUG
	mdelay(10);
#endif
        
	sdio_release_host(sctrl->func);
}

void sif_disable_irq(struct esp_pub *epub) 
{
        int err;
        struct esp_sdio_ctrl *sctrl = (struct esp_sdio_ctrl *)epub->sif;
        int i = 0;
                
        if (atomic_read(&sctrl->irq_installed) == 0)
                return;
        
	    sdio_claim_host(sctrl->func);

        while (atomic_read(&sctrl->irq_handling)) {
                sdio_release_host(sctrl->func);
                schedule_timeout(HZ / 100);
                sdio_claim_host(sctrl->func);
                if (i++ >= 400) {
                        esp_dbg(ESP_DBG_ERROR, "%s force to stop irq\n", __func__);
                        break;
                }
        }

        err = sdio_release_irq(sctrl->func);

        if (err) {
                esp_dbg(ESP_DBG_ERROR, "%s release irq failed\n", __func__);
        }

        atomic_set(&sctrl->irq_installed, 0);

        sdio_release_host(sctrl->func);

}

void sif_set_clock(struct sdio_func *func, int clk)
{
	struct mmc_host *host = NULL;
	struct mmc_card *card = NULL;
	
	card = func->card;
	host = card->host;

	sdio_claim_host(func);

	//currently only set clock
	host->ios.clock = clk * 1000000;

	esp_dbg(ESP_SHOW, "%s clock is %u\n", __func__, host->ios.clock);
	if (host->ios.clock > host->f_max) {
		host->ios.clock = host->f_max;
	}
	host->ops->set_ios(host, &host->ios);

	mdelay(2);

	sdio_release_host(func);
}

static int esp_sdio_probe(struct sdio_func *func, const struct sdio_device_id *id);
static void esp_sdio_remove(struct sdio_func *func);

static int esp_sdio_probe(struct sdio_func *func, const struct sdio_device_id *id) 
{
        int err;
        struct esp_pub *epub;
        struct esp_sdio_ctrl *sctrl;

        esp_dbg(ESP_DBG_TRACE,
                        "sdio_func_num: 0x%X, vendor id: 0x%X, dev id: 0x%X, block size: 0x%X/0x%X\n",
                        func->num, func->vendor, func->device, func->max_blksize,
                        func->cur_blksize);
	if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT){
		sctrl = kzalloc(sizeof(struct esp_sdio_ctrl), GFP_KERNEL);

		if (sctrl == NULL) {
			assert(0);
			return -ENOMEM;
		}

		/* temp buffer reserved for un-dma-able request */
		sctrl->dma_buffer = kzalloc(ESP_DMA_IBUFSZ, GFP_KERNEL);

		if (sctrl->dma_buffer == NULL) {
			assert(0);
			goto _err_last;
		}
		sif_sctrl = sctrl;
        	sctrl->slc_blk_sz = SIF_SLC_BLOCK_SIZE;
        	
		epub = esp_pub_alloc_mac80211(&func->dev);

        	if (epub == NULL) {
                	esp_dbg(ESP_DBG_ERROR, "no mem for epub \n");
                	err = -ENOMEM;
                	goto _err_dma;
        	}
        	epub->sif = (void *)sctrl;
        	sctrl->epub = epub;
	} else {
		ASSERT(sif_sctrl != NULL);
		sctrl = sif_sctrl;
		sif_sctrl = NULL;
		epub = sctrl->epub;
		SET_IEEE80211_DEV(epub->hw, &func->dev);
		epub->dev = &func->dev;
	}

        epub->sdio_state = sif_sdio_state;

        sctrl->func = func;
        sdio_set_drvdata(func, sctrl);

        sctrl->id = id;
        sctrl->off = true;

        /* give us some time to enable, in ms */
        func->enable_timeout = 100;

        err = esdio_power_on(sctrl);
        esp_dbg(ESP_DBG_TRACE, " %s >> power_on err %d \n", __func__, err);

        if (err){
                if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT)
                	goto _err_epub;
		  else
			goto _err_second_init;
        }
        check_target_id(func);

        sdio_claim_host(func);

        err = sdio_set_block_size(func, sctrl->slc_blk_sz);

        if (err) {
                esp_dbg(ESP_DBG_ERROR, "Set sdio block size %d failed: %d)\n",
                                sctrl->slc_blk_sz, err);
                sdio_release_host(func);
                goto _err_off;
        }

        sdio_release_host(func);

#ifdef SDIO_TEST
        sif_test_tx(sctrl);
#else

#ifdef LOWER_CLK 
        /* fix clock for dongle */
	sif_set_clock(func, 23);
#endif //LOWER_CLK

        err = esp_pub_init_all(epub);

        if (err) {
                esp_dbg(ESP_DBG_ERROR, "esp_init_all failed: %d\n", err);
                if(sif_sdio_state == ESP_SDIO_STATE_SECOND_INIT)
			goto _err_second_init;
        }

#endif //SDIO_TEST
        esp_dbg(ESP_DBG_TRACE, " %s return  %d\n", __func__, err);
	if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT){
		printk("first normal exit\n");
		sif_sdio_state = ESP_SDIO_STATE_FIRST_NORMAL_EXIT;
		up(&esp_powerup_sem);
	}

        return err;

_err_off:
        esdio_power_off(sctrl);
_err_epub:
        esp_pub_dealloc_mac80211(epub);
_err_dma:
        kfree(sctrl->dma_buffer);
_err_last:
        kfree(sctrl);

	if(sif_sdio_state == ESP_SDIO_STATE_FIRST_INIT){
		sif_sdio_state = ESP_SDIO_STATE_FIRST_ERROR_EXIT;
		up(&esp_powerup_sem);
	}
        return err;
_err_second_init:
	sif_sdio_state = ESP_SDIO_STATE_SECOND_ERROR_EXIT;
	esp_sdio_remove(func);
	return err;
}

#ifdef SIF_DEBUG_DSR_DUMP_REG
static void dump_slc_regs(struct slc_host_regs *regs) 
{
        esp_dbg(ESP_DBG_TRACE, "\n\n ------- %s --------------\n", __func__);

        esp_dbg(ESP_DBG_TRACE, " \
                        intr_raw 0x%08X \t \n  \
                        state_w0 0x%08X \t state_w1 0x%08X \n  \
                        config_w0 0x%08X \t config_w1 0x%08X \n \
                        intr_status 0x%08X \t config_w2 0x%08X \n \
                        config_w3 0x%08X \t config_w4 0x%08X \n \
                        token_wdata 0x%08X \t intr_clear 0x%08X \n \
                        intr_enable 0x%08X \n\n", regs->intr_raw, \
                        regs->state_w0, regs->state_w1, regs->config_w0, regs->config_w1, \
                        regs->intr_status, \
                        regs->config_w2, regs->config_w3, regs->config_w4, regs->token_wdata, \
                        regs->intr_clear, regs->intr_enable);
}
#endif /* SIF_DEBUG_DSR_DUMP_REG */

static void esp_sdio_remove(struct sdio_func *func) 
{
        struct esp_sdio_ctrl *sctrl = NULL;

        sctrl = sdio_get_drvdata(func);

        if (sctrl == NULL) {
                esp_dbg(ESP_DBG_ERROR, "%s no sctrl\n", __func__);
                return;
        }

        do {
                if (sctrl->epub == NULL) {
                        esp_dbg(ESP_DBG_ERROR, "%s epub null\n", __func__);
                        break;
                }
		sctrl->epub->sdio_state = sif_sdio_state;
		if(sif_sdio_state != ESP_SDIO_STATE_FIRST_NORMAL_EXIT){
			do{
				u8 low_byte = 0x80;
				int err;
				sdio_claim_host(func);
				sdio_writeb(func, low_byte, SLC_HOST_CONF_W4 + 2, &err);
				sdio_release_host(func);
			}while(0);
	
                	if (sctrl->epub->sip) {
                        	sip_detach(sctrl->epub->sip);
                        	sctrl->epub->sip = NULL;
                        	esp_dbg(ESP_DBG_TRACE, "%s sip detached \n", __func__);
                	}
		} else {
			//sif_disable_target_interrupt(sctrl->epub);
			atomic_set(&sctrl->epub->sip->state, SIP_STOP);
			sif_disable_irq(sctrl->epub);
		}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0))
                esdio_power_off(sctrl);
                esp_dbg(ESP_DBG_TRACE, "%s power off \n", __func__);
#endif /* kernel < 3.3.0 */

#ifdef TEST_MODE
                test_exit_netlink();
#endif /* TEST_MODE */
		if(sif_sdio_state != ESP_SDIO_STATE_FIRST_NORMAL_EXIT){
                	esp_pub_dealloc_mac80211(sctrl->epub);
                	esp_dbg(ESP_DBG_TRACE, "%s dealloc mac80211 \n", __func__);
			
			if (sctrl->dma_buffer) {
				kfree(sctrl->dma_buffer);
				sctrl->dma_buffer = NULL;
				esp_dbg(ESP_DBG_TRACE, "%s free dma_buffer \n", __func__);
			}

			kfree(sctrl);
		}

        } while (0);
        
	sdio_set_drvdata(func,NULL);
	
        esp_dbg(ESP_DBG_TRACE, "eagle sdio remove complete\n");
}

MODULE_DEVICE_TABLE(sdio, esp_sdio_devices);

static int esp_sdio_suspend(struct device *dev)
{
    struct sdio_func *func = dev_to_sdio_func(dev);
	struct esp_sdio_ctrl *sctrl = sdio_get_drvdata(func);
	struct esp_pub *epub = sctrl->epub;	

        printk("%s", __func__);
#if 0
	sip_send_suspend_config(epub, 1);
#endif
	atomic_set(&epub->ps.state, ESP_PM_ON);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34))
    do{
        u32 sdio_flags = 0;
        int ret = 0;
        sdio_flags = sdio_get_host_pm_caps(func);

        if (!(sdio_flags & MMC_PM_KEEP_POWER)) {
            printk("%s can't keep power while host is suspended\n", __func__);
        }

        /* keep power while host suspended */
        ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
        if (ret) {
                printk("%s error while trying to keep power\n", __func__);
        }
    }while(0);
#endif


        return 0;

}

static int esp_sdio_resume(struct device *dev)
{
        printk("%s", __func__);

        return 0;
}

static const struct dev_pm_ops esp_sdio_pm_ops = {
        .suspend= esp_sdio_suspend,
        .resume= esp_sdio_resume,
};

static struct sdio_driver esp_sdio_driver = {
                .name = "eagle_sdio",
                .id_table = esp_sdio_devices,
                .probe = esp_sdio_probe,
                .remove = esp_sdio_remove,
                .drv = { .pm = &esp_sdio_pm_ops, },
};

static int esp_sdio_dummy_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
        printk("%s enter\n", __func__);

        up(&esp_powerup_sem);
        
        return 0;
}

static void esp_sdio_dummy_remove(struct sdio_func *func) 
{
        return;
}

static struct sdio_driver esp_sdio_dummy_driver = {
                .name = "eagle_sdio_dummy",
                .id_table = esp_sdio_devices,
                .probe = esp_sdio_dummy_probe,
                .remove = esp_sdio_dummy_remove,
};


static int __init esp_sdio_init(void) 
{
#define ESP_WAIT_UP_TIME_MS 11000
        int err;
        u64 ver;
        int retry = 3;
        bool powerup = false;
        int edf_ret = 0;

        esp_dbg(ESP_DBG_TRACE, "%s \n", __func__);

#ifdef DRIVER_VER
        ver = DRIVER_VER;
        esp_dbg(ESP_SHOW, "\n*****%s %s EAGLE DRIVER VER:%llx*****\n\n", __DATE__, __TIME__, ver);
#endif
        edf_ret = esp_debugfs_init();

#ifdef ANDROID
	android_request_init_conf();
#endif /* defined(ANDROID)*/

        esp_wakelock_init();
        esp_wake_lock();

        do {
                sema_init(&esp_powerup_sem, 0);

                sif_platform_target_poweron();

                sif_platform_rescan_card(1);

                err = sdio_register_driver(&esp_sdio_dummy_driver);
                if (err) {
                        esp_dbg(ESP_DBG_ERROR, "eagle sdio driver registration failed, error code: %d\n", err);
                        goto _fail;
                }

                if (down_timeout(&esp_powerup_sem,
                                 msecs_to_jiffies(ESP_WAIT_UP_TIME_MS)) == 0) 
		{

                        powerup = true;
                        break;
                }

                esp_dbg(ESP_SHOW, "%s ------ RETRY ------ \n", __func__);

		sif_record_retry_config();

                sdio_unregister_driver(&esp_sdio_dummy_driver);

                sif_platform_rescan_card(0);
                
                sif_platform_target_poweroff();
                
        } while (retry--);

        if (!powerup) {
                esp_dbg(ESP_DBG_ERROR, "eagle sdio can not power up!\n");

                err = -ENODEV;
                goto _fail;
        }

        esp_dbg(ESP_SHOW, "%s power up OK\n", __func__);

        sdio_unregister_driver(&esp_sdio_dummy_driver);

        sif_sdio_state = ESP_SDIO_STATE_FIRST_INIT;
        sema_init(&esp_powerup_sem, 0);

        sdio_register_driver(&esp_sdio_driver);

        if (down_timeout(&esp_powerup_sem,
                                 msecs_to_jiffies(ESP_WAIT_UP_TIME_MS)) == 0) 
	{
		if(sif_sdio_state == ESP_SDIO_STATE_FIRST_NORMAL_EXIT){
                	sdio_unregister_driver(&esp_sdio_driver);

                	sif_platform_rescan_card(0);
			
			msleep(80);  
                
			sif_platform_rescan_card(1);

			sif_sdio_state = ESP_SDIO_STATE_SECOND_INIT;
        	
			sdio_register_driver(&esp_sdio_driver);
		}
                
        }


        esp_register_early_suspend();
	esp_wake_unlock();
        return err;

_fail:
        esp_wake_unlock();
        esp_wakelock_destroy();

        return err;
}

static void __exit esp_sdio_exit(void) 
{
	esp_dbg(ESP_DBG_TRACE, "%s \n", __func__);

	esp_debugfs_exit();
	
        esp_unregister_early_suspend();

	sdio_unregister_driver(&esp_sdio_driver);
	
	sif_platform_rescan_card(0);

#ifndef FPGA_DEBUG
	sif_platform_target_poweroff();
#endif /* !FPGA_DEBUG */

        //esp_wake_unlock();
        esp_wakelock_destroy();
	
}

static int bt_config = 0;
void sif_record_bt_config(int value)
{
	bt_config = value;
}

int sif_get_bt_config(void)
{
	return bt_config;
}

static int rst_config = 0;
void sif_record_rst_config(int value)
{
        rst_config = value;
}

int sif_get_rst_config(void)
{
        return rst_config;
}

static int retry_reset = 0;
void sif_record_retry_config(void)
{
	retry_reset = 1;
}

int sif_get_retry_config(void)
{
	return retry_reset;
}

#include "sdio_stub.c"

MODULE_AUTHOR("Espressif System");
MODULE_DESCRIPTION("Driver for SDIO interconnected eagle low-power WLAN devices");
MODULE_LICENSE("GPL");

