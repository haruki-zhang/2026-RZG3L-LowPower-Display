FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

# Keep the list in numeric order. do_patch applies it in this order, and each
# patch builds on the ones before it. "devtool finish" writes it unordered.
SRC_URI += " \
	file://0001-arm64-dts-renesas-rzg3l-smarc-reserve-the-U-Boot-boo.patch \
	file://0002-arm64-dts-renesas-rzg3l-smarc-declare-the-boot-logo-.patch \
	file://0003-clk-renesas-r9a08g046-mark-the-MIPI-DSI-and-LCDC-clo.patch \
	file://0004-media-vsp1-take-over-a-pipeline-left-running-by-the-.patch \
	file://0005-drm-rz-du-inherit-a-CRTC-left-running-by-the-bootloa.patch \
	file://0006-drm-rz-du-rzg2l_mipi_dsi-inherit-a-link-left-streami.patch \
	file://0007-drm-bridge-adv7511-keep-an-HDMI-output-configured-by.patch \
	file://0008-drm-bridge-adv7511-pin-the-connector-to-the-bootload.patch \
	file://seamless-logo-still.cfg \
"
