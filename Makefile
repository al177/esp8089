include drivers/net/wireless/esp8089/esp_config.mk

#EXTRA_CFLAGS += -DDEBUG -DSIP_DEBUG -DFAST_TX_STATUS -DANDROID -DKERNEL_IV_WAR -DRX_SENDUP_SYNC -DDEBUGFS -DHAS_FW -DTEST_MODE -DINIT_DATA_CONF -DHAS_INIT_DATA
EXTRA_CFLAGS += -DDEBUG -DSIP_DEBUG -DFAST_TX_STATUS -DANDROID -DKERNEL_IV_WAR -DRX_SENDUP_SYNC -DDEBUGFS -DHAS_FW -DTEST_MODE -DHAS_INIT_DATA

obj-$(CONFIG_ESP8089) := wlan.o
wlan-y += esp_debug.o
wlan-y += sdio_sif_esp.o
wlan-y += esp_android.o
wlan-y += esp_main.o
wlan-y += esp_sip.o
wlan-y += esp_ctrl.o
wlan-y += esp_mac80211.o
wlan-y += esp_debug.o
wlan-y += esp_utils.o
wlan-y += esp_pm.o
wlan-y += testmode.o

