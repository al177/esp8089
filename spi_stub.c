/*
 * Copyright (c) 2013 Espressif System.
 *
 *  sdio stub code for allwinner
 */
#include <asm/io.h>
#include <mach/irqs.h>
#include <mach/io.h>
#include <mach/iomux.h>
#include <mach/pmu.h>
#include <linux/gpio.h>
#include <asm/gpio.h>
#include <asm/mach/irq.h>

#include "../drivers/spi/rk29_spim.h"
#include "esp_sif.h"

//#define SPI_FREQ (20000000)                             //  1. 22.5Mhz     2. 45Mhz
#define SPI_FREQ (30000000)                             //  1. 22.5Mhz     2. 45Mhz

//Below are for spi HZ 22.5M
#if (SPI_FREQ == 30000000)

#define CMD_RESP_SIZE   (10) //(50)    //Common respon wait time
#define DATA_RESP_SIZE_W   (142+45) // (1024*13)//   (1024*16)  //(398+400) // (1024*10)    //Only for Write bytes function, data write response.  max:(361+109) 
#define DATA_RESP_SIZE_R   (231+75) //  (340+102)  //(231+75)//(340+102)   //Only for Read bytes function, data write response    max:(340+102) 

#define BLOCK_W_DATA_RESP_SIZE_EACH          (10)           //For each data write resp size, in block write 
#define BLOCK_W_DATA_RESP_SIZE_FINAL (152) // (142+52)   //For final data write resp size, in block write ,max: 119

#define BLOCK_R_DATA_RESP_SIZE_1ST   (265)  // (231+75)    //For each data read resp size, in block read  ,max: 134
#define BLOCK_R_DATA_RESP_SIZE_EACH    (10)  // (20)   //For each data read resp size, in block read 

#elif(SPI_FREQ == 20000000)

#define CMD_RESP_SIZE (10)    //Common respon wait time
#define DATA_RESP_SIZE_W (103+40)    //Only for Write bytes function, data write response.  max: 103
#define DATA_RESP_SIZE_R (118+40)    //Only for Read bytes function, data write response  max: 118
//w: oxFF : 218 clock.     oxFE : 214 clock.  

#define BLOCK_W_DATA_RESP_SIZE_EACH          (20)           //For each data write resp size, in block write 
#define BLOCK_W_DATA_RESP_SIZE_FINAL     (112+40)    //For final data write resp size, in block write ,max :112

#define BLOCK_R_DATA_RESP_SIZE_1ST          (123+40)   //For each data read resp size, in block read ,max: 123
#define BLOCK_R_DATA_RESP_SIZE_EACH       (20)   //For each data read resp size, in block read 

#endif

//0xE5 ~0xFF  30us totoal 
//

struct spi_device_id esp_spi_id[] = {
        {"esp_spi_0", 0},
        {"esp_spi_1", 1},
        {},
};


#ifdef  REGISTER_SPI_BOARD_INFO
static struct rk29xx_spi_chip spi_test_chip[] = {
{
        //.poll_mode = 1,
        .enable_dma = 1,
},
{
        //.poll_mode = 1,
        .enable_dma = 1,
},

};
static struct spi_board_info esp_board_spi_devices[] = {       
        {
                .modalias  = "esp_spi_0",
                .bus_num = 0,   //0 or 1
                .max_speed_hz  = 18*1000*1000,
                .chip_select   = 0,             
                .mode   = SPI_MODE_3,
                .controller_data = &spi_test_chip[0],
        },
};

void sif_platform_register_board_info(void) {
        spi_register_board_info(esp_board_spi_devices, ARRAY_SIZE(esp_board_spi_devices));
}
#endif  /*REGISTER_SPI_BOARD_INFO*/


#define RK30_GPIO0_BASE          RK2928_GPIO0_BASE
#define GPIO_NO                  RK30_PIN0_PA0
#define GPIO_BASE_ADDR           ((unsigned char __iomem *) RK30_GPIO0_BASE)
#define GPIO_INT_MASK_OFFSET     GPIO_INTEN
#define GPIO_INT_STAT_OFFSET     GPIO_PORTS_EOI

int sif_platform_get_irq_no(void)
{
	return gpio_to_irq(GPIO_NO);
}

int sif_platform_is_irq_occur(void)
{      
	return 1;
}

void sif_platform_irq_clear(void)
{
}

void sif_platform_irq_mask(int mask)
{
	if (mask)
		disable_irq_nosync(sif_platform_get_irq_no());
	else
		enable_irq(sif_platform_get_irq_no());
}

int sif_platform_irq_init(void)
{
	int ret;

	printk(KERN_ERR "%s enter\n", __func__);

	if ( (ret = gpio_request(GPIO_NO, "esp_spi_int")) != 0) {
		printk(KERN_ERR "request gpio error\n");
		return ret;
	}

	gpio_direction_input(GPIO_NO);

        sif_platform_irq_clear();
	sif_platform_irq_mask(1);

        udelay(1);

	return 0;
}


void sif_platform_irq_deinit(void)
{
	gpio_free(GPIO_NO);
}


void sif_platform_reset_target(void)
{
        gpio_direction_output(RK30_PIN1_PB3, GPIO_LOW);
        mdelay(200);
        gpio_direction_output(RK30_PIN1_PB3, GPIO_HIGH);
        mdelay(200);
}

void sif_platform_target_poweroff(void)
{
        gpio_direction_output(RK30_PIN1_PB3, GPIO_LOW);

}

void sif_platform_target_poweron(void)
{
        mdelay(200);
        gpio_direction_output(RK30_PIN1_PB3, GPIO_LOW);
        mdelay(200);
        gpio_direction_output(RK30_PIN1_PB3, GPIO_HIGH);
        mdelay(200);
}

void sif_platform_target_speed(int high_speed)
{
}

#ifdef ESP_ACK_INTERRUPT
void sif_platform_ack_interrupt(struct esp_pub *epub)
{
	sif_platform_irq_clear();
}
#endif //ESP_ACK_INTERRUPT


module_init(esp_spi_init);
module_exit(esp_spi_exit);

