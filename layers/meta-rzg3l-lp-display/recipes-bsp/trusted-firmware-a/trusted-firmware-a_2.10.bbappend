# RZ/G3L LowPower Display demo: DISP_RET system suspend (display kept running by the CM33)
#
# Replaces the BSP default PLAT_SYSTEM_SUSPEND=vbat (SoC power-off through the PMIC, which
# would stop the CM33 and the display and use RIIC0). The patch applies on top of the
# meta-rz-multi-os TF-A patches (BL22 boot, SRAM layout); this layer has a higher priority.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:smarc-rzg3l = " \
    file://0001-plat-g3l-add-the-DISP_RET-suspend-mode-LowPower-Disp.patch \
"

EXTRA_OEMAKE:remove:smarc-rzg3l = "PLAT_SYSTEM_SUSPEND=vbat"
EXTRA_OEMAKE:append:smarc-rzg3l = " PLAT_SYSTEM_SUSPEND=disp"
