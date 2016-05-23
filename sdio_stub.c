/*
 * Copyright (c) 2013 Espressif System.
 *
 *  sdio stub code for RK
 */

#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
//#include <mach/iomux.h>

/* reset GPIO parameter defaults to GPIO 0 (ID_SD) on the Raspberry Pi */
static int esp_reset_gpio = 0;
module_param(esp_reset_gpio, int, 0);
MODULE_PARM_DESC(esp_reset_gpio, "ESP8089 CH_PD reset GPIO number");

#define ESP8089_DRV_VERSION "1.9"

extern int rk29sdk_wifi_power(int on);
extern int rk29sdk_wifi_set_carddetect(int val);
int rockchip_wifi_init_module(void)
{	
	return esp_sdio_init();		
}

void rockchip_wifi_exit_module(void)
{
	esp_sdio_exit(); 
		 
}
void sif_platform_rescan_card(unsigned insert)
{
}

void sif_platform_reset_target(void)
{
	printk("ESP8089 reset via GPIO %d\n", esp_reset_gpio);
	gpio_request(esp_reset_gpio,"esp_reset");
	gpio_direction_output(esp_reset_gpio,0);
	msleep(200);
	gpio_direction_input(esp_reset_gpio);
	gpio_free(esp_reset_gpio);
}

void sif_platform_target_poweroff(void)
{
	/* reset ESP before unload so that the esp can be probed on
	 * warm reboot */
	sif_platform_reset_target();
}

void sif_platform_target_poweron(void)
{
	sif_platform_reset_target();
}

void sif_platform_target_speed(int high_speed)
{
}

void sif_platform_check_r1_ready(struct esp_pub *epub)
{
}


#ifdef ESP_ACK_INTERRUPT
extern void sdmmc_ack_interrupt(struct mmc_host *mmc);

void sif_platform_ack_interrupt(struct esp_pub *epub)
{
        struct esp_sdio_ctrl *sctrl = NULL;
        struct sdio_func *func = NULL;

	if (epub == NULL) {
        	ESSERT(epub != NULL);
		return;
	}
        sctrl = (struct esp_sdio_ctrl *)epub->sif;
        func = sctrl->func;
	if (func == NULL) {
        	ESSERT(func != NULL);
		return;
	}

        sdmmc_ack_interrupt(func->card->host);
}
#endif //ESP_ACK_INTERRUPT
 EXPORT_SYMBOL(rockchip_wifi_init_module);
 EXPORT_SYMBOL(rockchip_wifi_exit_module);

late_initcall(esp_sdio_init);
module_exit(esp_sdio_exit);
