esp8089
======

ESP8089 Linux driver

v1.9 imported from the Rockchip Linux kernel github repo

Modified to build as a standalone module for SDIO devices.




Building:

 make
 sudo make install

Using:

modprobe should autoload the module as the ESP8089 should be detected by
the SDIO driver and loaded as a dependency.

The ESP8089 requires that the CH_PD signal be reset before the driver
can load properly, so a GPIO is used by the driver to assert this signal
for 200ms.  The GPIO defaults to 0 (ID_SD on the Raspberry Pi) but this
can be changed to any GPIO mapped by the kernel GPIO driver through the
esp_reset_gpio module parameter.  This can be accomplished by creating a
new modprobe.d config file.  For example, to use GPIO 5 instead, create
as root /etc/modprobe.d/esp.conf and add the line:

 options esp8089 esp_reset_gpio=5

which changes the GPIO from 0 to 5.
