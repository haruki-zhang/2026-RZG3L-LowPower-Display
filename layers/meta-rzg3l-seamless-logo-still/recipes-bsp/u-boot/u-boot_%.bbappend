FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

# Keep the numeric order: 0003-0005 change the driver added by 0002.
# 0001-0004 are the same as in meta-rzg3l-seamless-logo-sequence.
SRC_URI += " \
	file://0001-configs-smarc-rzg3l-add-CONFIG_VIDEO-y.patch \
	file://0002-add-Display-Drivers.patch \
	file://0003-video-rzg2l-load-the-boot-logo-from-the-rootfs-parti.patch \
	file://0004-video-rzg2l-wait-for-two-frames-instead-of-a-flat-se.patch \
	file://0005-video-rzg2l-let-the-DU-recover-from-a-PBUF-underflow.patch \
"
