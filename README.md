esp8089
======

ESP8089 Linux driver

v1.9 imported from the Rockchip Linux kernel github repo

Modified to build as a standalone module for SDIO devices.




Building:

 make

Using:

Must load mac80211.ko first if not baked in.

 sudo modprobe esp8089.ko
