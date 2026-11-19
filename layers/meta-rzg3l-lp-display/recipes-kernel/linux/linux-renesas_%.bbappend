# RZ/G3L LowPower Display demo
#
# The patches apply on top of meta-rz-multi-os (OSTM2 critical clock, multi-os dtsi) and
# meta-rzg3l-seamless-logo-still (seamless display hand-over); this layer has a higher priority.
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append:smarc-rzg3l = " \
    file://0001-clk-renesas-r9a08g046-keep-the-RIIC0-clock-running-f.patch \
    file://0002-arm64-dts-renesas-rzg3l-smarc-hand-RIIC0-over-to-the.patch \
    file://0003-arm64-dts-renesas-rzg3l-smarc-CM33-display-hand-over.patch \
    file://0004-drm-rz-du-hand-the-running-display-over-to-the-CM33-.patch \
    file://0005-drm-rz-du-keep-the-display-running-through-the-syste.patch \
    file://0006-media-vsp1-drm-rz-du-rzg2l_mipi_dsi-keep-running-thr.patch \
    file://0007-clk-renesas-rzg2l-cpg-only-restore-the-clock-setting.patch \
    file://0008-arm64-dts-renesas-rzg3l-smarc-keep-the-display-runni.patch \
    file://0009-arm64-dts-renesas-rzg3l-smarc-1080p-output-and-both-.patch \
    file://lpdemo.cfg \
"
