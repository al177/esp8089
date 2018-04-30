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


Building a Debian source package:

To build a source package for release, first install the following:

apt-get install debhelper dkms raspberrypi-kernel-headers

Then build the package:

sudo make dkmsdeb

The module will be named esp8089-dkms_*.deb.  Don't forget to update the version
in dkms.conf and committing before making a release!


If the build fails to create a source package, try applying the following patch from https://bugs.launchpad.net/ubuntu/+source/dkms/+bug/1729051:

--- a/etc/dkms/template-dkms-mkdeb/debian/control	2017-10-31 14:40:41.690069116 -0300
+++ b/etc/dkms/template-dkms-mkdeb/debian/control	2017-10-31 14:41:12.137973994 -0300
@@ -6,6 +6,6 @@
 Standards-Version: 3.8.1

 Package: DEBIAN_PACKAGE-dkms
-Architecture: all
+Architecture: DEBIAN_BUILD_ARCH
 Depends: dkms (>= 1.95), ${misc:Depends}
 Description: DEBIAN_PACKAGE driver in DKMS format.


