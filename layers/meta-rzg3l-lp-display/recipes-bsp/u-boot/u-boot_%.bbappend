# RZ/G3L LowPower Display demo: 1080p boot logo / output (Step 4)
#
# Applies on top of the meta-rzg3l-seamless-logo-still display driver (its patch 0002 adds
# drivers/video/rzg2l/rzg2l_video.c). The fast-logo bbappend uses "SRC_URI +=", which is
# evaluated before any ":append", so this patch is always applied after its patches.
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append:smarc-rzg3l = " \
    file://0001-video-rzg2l-start-the-boot-logo-at-1080p-LowPower-Di.patch \
"
