
/*
 * Copyright (c) 2011 Espressif System.
 *
 *     MAC80211 support module
 */
#ifdef TEST_MODE

#include <linux/etherdevice.h>
#include <linux/workqueue.h>
#include <linux/nl80211.h>
#include <linux/ieee80211.h>
#include <linux/slab.h>
#include <net/regulatory.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/genetlink.h>
#include "esp_pub.h"
#include "esp_sip.h"
#include "esp_ctrl.h"
#include "esp_sif.h"
#include "esp_debug.h"
#include "esp_wl.h"
#include "testmode.h"

static u32 connected_nl;
static struct genl_info info_copy;
static struct esp_sip *sip_copy;
static u8 *sdio_buff;

#define SIP sip_copy
#define OUT_DONE() \
        do { \
 	     printk(KERN_DEBUG "esp_sdio: error occured in %s\n", __func__); \
        } while(0)

/* ESP TEST netlinf family */
static struct genl_family test_genl_family = {
        .id = GENL_ID_GENERATE,
        .hdrsize = 0,
        .name = "esp_sdio",
        .version = 1,
        .maxattr = TEST_ATTR_MAX,
};

struct loopback_param_s {
        u32 packet_num;
        u32 packet_id;
};

static struct loopback_param_s loopback_param;
u32 get_loopback_num()
{
        return loopback_param.packet_num;
}

u32 get_loopback_id()
{
        return loopback_param.packet_id;
}

void inc_loopback_id()
{
        loopback_param.packet_id++;
}

#define REGISTER_REPLY(info) \
        memcpy((char *)&info_copy, (char *)(info), sizeof(struct genl_info))


static void sip_send_test_cmd(struct esp_sip *sip, struct sk_buff *skb)
{
        skb_queue_tail(&sip->epub->txq, skb);

#ifdef FPGA_LOOPBACK
        queue_work(sip->epub->esp_wkq, &sip->epub->tx_work);
#else
        ieee80211_queue_work(sip->epub->hw, &sip->epub->tx_work);
#endif /* FPGA_LOOPBACK */

}

static int esp_test_cmd_reply(struct genl_info *info, u32 cmd_type, char *reply_info)
{
        struct sk_buff *skb;
        void *hdr;

        /*directly send ask_info to target, and waiting for report*/
        skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
        if (skb == NULL)
                goto out;

        hdr = genlmsg_put(skb,  info->snd_pid, info->snd_seq, &test_genl_family, 0, cmd_type);
        if (hdr == NULL)
                goto nla_put_failure;

        NLA_PUT_STRING(skb, TEST_ATTR_STR, reply_info);

        genlmsg_end(skb, hdr);
        genlmsg_reply(skb, info);
        return 0;

nla_put_failure:
        nlmsg_free(skb);
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_echo(struct sk_buff *skb_2,
                         struct genl_info *info)
{
        char *echo_info;
        int res;

        if (info == NULL)
                goto out;

        connected_nl = info->snd_pid;
        printk(KERN_DEBUG "esp_sdio: received a echo, "
               "from pid %d\n", info->snd_pid);
	sip_debug_show(SIP);
	
        /*get echo info*/
        echo_info = nla_data(info->attrs[TEST_ATTR_STR]);

        res=esp_test_cmd_reply(info, TEST_CMD_ECHO, echo_info);
        return res;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_sdiospeed(struct sk_buff *skb_2,
                              struct genl_info *info)
{
        char *speed_info;
        int res;

        if (info == NULL)
                goto out;

        connected_nl = info->snd_pid;

        /*get echo info*/
        speed_info = nla_data(info->attrs[TEST_ATTR_STR]);

        printk(KERN_DEBUG "esp_sdio: received a sdio speed %s, "
               "from pid %d\n", speed_info, info->snd_pid);

        if (!strcmp(speed_info, "high")) {
                sif_platform_target_speed(1);
        } else if (!strcmp(speed_info, "low")) {
                sif_platform_target_speed(0);
        } else {
                printk(KERN_DEBUG "%s:  %s unsupported\n", __func__, speed_info);
        }

        res=esp_test_cmd_reply(info, TEST_CMD_SDIOSPEED, speed_info);
        return res;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_ask(struct sk_buff *skb_2,
                        struct genl_info *info)
{
        char *ask_info;
        int res;

        if (info == NULL)
                goto out;

        /*get echo info*/
        ask_info = nla_data(info->attrs[TEST_ATTR_STR]);

        /*directly send ask_info to target, and waiting for report*/
        res=esp_test_cmd_reply(info, TEST_CMD_ASK, "ok");
        return res;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_sleep(struct sk_buff *skb_2,
                          struct genl_info *info)
{
        struct sip_cmd_sleep *sleepcmd;
        struct sk_buff *skb = NULL;
        int res;

        if (info == NULL)
                goto out;

        skb = sip_alloc_ctrl_skbuf(SIP, sizeof(struct sip_cmd_sleep), SIP_CMD_SLEEP);
        if (!skb)
                goto out;

        sleepcmd = (struct sip_cmd_sleep *)(skb->data + sizeof(struct sip_tx_info));
        sleepcmd->sleep_mode       =  nla_get_u32(info->attrs[TEST_ATTR_PARA0]);
        sleepcmd->sleep_tm_ms     =  nla_get_u32(info->attrs[TEST_ATTR_PARA1]);
        sleepcmd->wakeup_tm_ms  =  nla_get_u32(info->attrs[TEST_ATTR_PARA2]);
        sleepcmd->sleep_times       =  nla_get_u32(info->attrs[TEST_ATTR_PARA3]);

        sip_send_test_cmd(SIP, skb);

        /*directly send ask_info to target, and waiting for report*/
        res=esp_test_cmd_reply(info, TEST_CMD_SLEEP, "ok");
        return res;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_wakeup(struct sk_buff *skb_2,
                           struct genl_info *info)
{
        struct sip_cmd_wakeup *wakeupcmd;
        struct sk_buff *skb = NULL;
        //int res;

        if (info == NULL)
                goto out;

        skb = sip_alloc_ctrl_skbuf(SIP, sizeof(struct sip_cmd_wakeup), SIP_CMD_WAKEUP);
        if (!skb)
                goto out;
        wakeupcmd = (struct sip_cmd_wakeup *)(skb->data + sizeof(struct sip_tx_info));
        wakeupcmd->check_data = nla_get_u32(info->attrs[TEST_ATTR_PARA0]);

        /*directly send reply_info to target, and waiting for report*/
        REGISTER_REPLY(info);
        //res=esp_test_cmd_reply(info, TEST_CMD_WAKEUP, "ok");

        sip_send_test_cmd(SIP, skb);
        return 0;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_loopback(struct sk_buff *skb_2,
                             struct genl_info *info)
{
        u32 txpacket_len;
        u32 rxpacket_len;

        if (info == NULL)
                goto out;

        txpacket_len   = nla_get_u32(info->attrs[TEST_ATTR_PARA0]);
        rxpacket_len   = nla_get_u32(info->attrs[TEST_ATTR_PARA1]);
        loopback_param.packet_num    = nla_get_u32(info->attrs[TEST_ATTR_PARA2]);
        loopback_param.packet_id=0;
        REGISTER_REPLY(info);
        return sip_send_loopback_mblk(SIP, txpacket_len, rxpacket_len, 0);
out:
        OUT_DONE();
        return -EINVAL;
}

/*
u8 probe_req_frm[] = {0x40,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x03,0x8F,0x11,0x22,0x88,
                      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x01,0x08,0x82,0x84,0x8B,0x96,
                      0x0C,0x12,0x18,0x24,0x32,0x04,0x30,0x48,0x60,0x6C
                     };
*/

static int sip_send_tx_frame(struct esp_sip *sip, u32 packet_len)
{

        struct sk_buff *skb = NULL;
        u8 *ptr = NULL;
        int i;

        skb = alloc_skb(packet_len, GFP_KERNEL);
        skb->len = packet_len;
        ptr = skb->data;
        /* fill up pkt payload */
        for (i = 0; i < skb->len; i++) {
                ptr[i] = i;
        }
#ifdef FPGA_LOOPBACK
        skb_queue_tail(&sip->epub->txq, skb);
        queue_work(sip->epub->esp_wkq, &sip->epub->tx_work);
#else
        sip_tx_data_pkt_enqueue(sip->epub, skb);
        ieee80211_queue_work(sip->epub->hw, &sip->epub->tx_work);
#endif
        return 0;
}

static int esp_test_tx(struct sk_buff *skb_2,
                       struct genl_info *info)
{
        u32 txpacket_len;
        u32 res;

        if (info == NULL)
                goto out;

        txpacket_len     = nla_get_u32(info->attrs[TEST_ATTR_PARA0]);
        REGISTER_REPLY(info);
        sip_send_tx_frame(SIP, txpacket_len);
        res=esp_test_cmd_reply(info, TEST_CMD_TX, "tx out");
        return res;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_genl(struct sk_buff *skb_2,
                         struct genl_info *info)
{
        struct sip_cmd_debug *dbgcmd;
        struct sk_buff *skb = NULL;
        int i;

        if (info == NULL)
                goto out;

        skb = sip_alloc_ctrl_skbuf(SIP, sizeof(struct sip_cmd_debug), SIP_CMD_DEBUG);
        if (!skb)
                goto out;

        dbgcmd = (struct sip_cmd_debug *)(skb->data + sizeof(struct sip_hdr));
        dbgcmd->cmd_type = nla_get_u32(info->attrs[TEST_ATTR_CMD_TYPE]);
        dbgcmd->para_num = nla_get_u32(info->attrs[TEST_ATTR_PARA_NUM]);
        printk(KERN_DEBUG "%s dbgcmdType %d paraNum %d\n", __func__, dbgcmd->cmd_type, dbgcmd->para_num);
        for (i=0; i<dbgcmd->para_num; i++)
                dbgcmd->para[i] = nla_get_u32(info->attrs[TEST_ATTR_PARA(i)]);

        /*directly send reply_info to target, and waiting for report*/
        REGISTER_REPLY(info);

        sip_send_test_cmd(SIP, skb);
        return 0;
out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_sdio_wr(struct sk_buff *skb_2,
                            struct genl_info *info)
{
        int res;
        u32 func_no, addr, value;

        if (info == NULL)
                goto out;

        func_no = nla_get_u32(info->attrs[TEST_ATTR_PARA0]);
        addr = nla_get_u32(info->attrs[TEST_ATTR_PARA1]);
        value = nla_get_u32(info->attrs[TEST_ATTR_PARA2]);

        if(!func_no) {
                sdio_io_writeb( SIP->epub, (u8) value&0xff, addr, &res);
        } else {
                memcpy(sdio_buff, (u8 *)&value, 4);
                res=sif_io_sync(SIP->epub, addr, sdio_buff, 4, SIF_TO_DEVICE | SIF_BYTE_BASIS | SIF_INC_ADDR);
        }

        /*directly send reply_info to target, and waiting for report*/
        REGISTER_REPLY(info);
        if (!res)
                esp_test_cmd_reply(info, TEST_CMD_SDIO_WR, "write ok!");
        else
                esp_test_cmd_reply(info, TEST_CMD_SDIO_WR, "write fail!");

out:
        OUT_DONE();
        return -EINVAL;
}

static int esp_test_sdio_rd(struct sk_buff *skb_2,
                            struct genl_info *info)
{
        int res;
        u32 func_no, addr, value;
        char value_str[12];

        if (info == NULL)
                goto out;

        func_no = nla_get_u32(info->attrs[TEST_ATTR_PARA0]);
        addr = nla_get_u32(info->attrs[TEST_ATTR_PARA1]);

        if(!func_no) {
                memset(sdio_buff, 0, 4);
                sdio_buff[0]= sdio_io_readb(SIP->epub, addr, &res);
        } else {
                res=sif_io_sync(SIP->epub, addr, sdio_buff, 4, SIF_FROM_DEVICE | SIF_BYTE_BASIS | SIF_INC_ADDR);
        }
        memcpy((u8 *)&value, sdio_buff, 4);

        /*directly send reply_info to target, and waiting for report*/
        REGISTER_REPLY(info);
        if (!res) {
                sprintf((char *)&value_str, "0x%x", value);
                esp_test_cmd_reply(info, TEST_CMD_SDIO_RD, value_str);
        } else
                esp_test_cmd_reply(info, TEST_CMD_SDIO_RD, "read fail!");

out:
        OUT_DONE();
        return -EINVAL;
}

/* TEST_CMD netlink policy */

static struct nla_policy test_genl_policy[TEST_ATTR_MAX + 1] = {
        [TEST_ATTR_CMD_NAME]= { .type = NLA_NUL_STRING, .len = GENL_NAMSIZ - 1 },
        [TEST_ATTR_CMD_TYPE] = { .type = NLA_U32 },
        [TEST_ATTR_PARA_NUM] = { .type = NLA_U32 },
        [TEST_ATTR_PARA0] = { .type = NLA_U32 },
        [TEST_ATTR_PARA1] = { .type = NLA_U32 },
        [TEST_ATTR_PARA2] = { .type = NLA_U32 },
        [TEST_ATTR_PARA3] = { .type = NLA_U32 },
        [TEST_ATTR_PARA4] = { .type = NLA_U32 },
        [TEST_ATTR_PARA5] = { .type = NLA_U32 },
        [TEST_ATTR_PARA6] = { .type = NLA_U32 },
        [TEST_ATTR_PARA7] = { .type = NLA_U32 },
        [TEST_ATTR_STR]	= { .type = NLA_NUL_STRING, .len = 256-1 },
};

/* Generic Netlink operations array */
static struct genl_ops esp_test_ops[] = {
        {
                .cmd = TEST_CMD_ECHO,
                .policy = test_genl_policy,
                .doit = esp_test_echo,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_ASK,
                .policy = test_genl_policy,
                .doit = esp_test_ask,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_SLEEP,
                .policy = test_genl_policy,
                .doit = esp_test_sleep,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_WAKEUP,
                .policy = test_genl_policy,
                .doit = esp_test_wakeup,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_LOOPBACK,
                .policy = test_genl_policy,
                .doit = esp_test_loopback,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_TX,
                .policy = test_genl_policy,
                .doit = esp_test_tx,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_DEBUG,
                .policy = test_genl_policy,
                .doit = esp_test_genl,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_SDIO_WR,
                .policy = test_genl_policy,
                .doit = esp_test_sdio_wr,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_SDIO_RD,
                .policy = test_genl_policy,
                .doit = esp_test_sdio_rd,
                .flags = GENL_ADMIN_PERM,
        },
        {
                .cmd = TEST_CMD_SDIOSPEED,
                .policy = test_genl_policy,
                .doit = esp_test_sdiospeed,
                .flags = GENL_ADMIN_PERM,
        },
};

static int esp_test_netlink_notify(struct notifier_block *nb,
                                   unsigned long state,
                                   void *_notify)
{
        struct netlink_notify *notify = _notify;

        if (state != NETLINK_URELEASE)
                return NOTIFY_DONE;

        if (notify->pid == connected_nl) {
                printk(KERN_INFO "esp_sdio: user released netlink"
                       " socket \n");
                connected_nl = 0;
        }
        return NOTIFY_DONE;

}

static struct notifier_block test_netlink_notifier = {
        .notifier_call = esp_test_netlink_notify,
};

int test_init_netlink(struct esp_sip *sip)
{
        int rc;
        printk(KERN_INFO "esp_sdio: initializing netlink\n");

        sip_copy=sip;
        /* temp buffer for sdio test */
        sdio_buff = kzalloc(8, GFP_KERNEL);

        rc = genl_register_family_with_ops(&test_genl_family,
                                           esp_test_ops, ARRAY_SIZE(esp_test_ops));
        if (rc)
                goto failure;

        rc = netlink_register_notifier(&test_netlink_notifier);
        if (rc)
                goto failure;

        return 0;

failure:
        printk(KERN_DEBUG "esp_sdio: error occured in %s\n", __func__);
        kfree(sdio_buff);
        return -EINVAL;
}

void test_exit_netlink(void)
{
        int ret;

	if (sdio_buff == NULL)
		return;

        kfree(sdio_buff);
        printk(KERN_INFO "esp_sdio: closing netlink\n");
        /* unregister the notifier */
        netlink_unregister_notifier(&test_netlink_notifier);
        /* unregister the family */
        ret = genl_unregister_family(&test_genl_family);
        if (ret)
                printk(KERN_DEBUG "esp_sdio: "
                       "unregister family %i\n", ret);
}
void esp_test_cmd_event(u32 cmd_type, char *reply_info)
{
        esp_test_cmd_reply(&info_copy, cmd_type, reply_info);
}
#endif  //ifdef TEST_MODE

