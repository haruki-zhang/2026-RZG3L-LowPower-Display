SUMMARY = "RZ/G3L LowPower Display Demo image"
DESCRIPTION = "core-image-weston of the RZ/G3L BSP plus the LowPower Display Demo \
(CM33 firmware, lpdemo-ctl, GUI)."

require recipes-graphics/images/core-image-weston.bb
# The same additions as meta-rz-distro's core-image-weston.bbappend
require include/rz-distro-common.inc
require include/rz-modules-common.inc

COMPATIBLE_MACHINE = "smarc-rzg3l"

IMAGE_INSTALL:append = " \
    lpdemo-cm33-fw \
    lpdemo-ctl \
    lpdemo-gui \
    kernel-module-uio-pdrv-genirq \
"

# eSD boot image with the CM33 firmware at the BL22 offset
WKS_FILE = "lpdemo-bootpart-esd.wks"
do_image_wic[depends] += "lpdemo-cm33-fw:do_deploy"
