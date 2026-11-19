# SPDX-License-Identifier: MIT
#
# U-Boot loads the logo with
#
#   ext4load mmc 0:2 0x48000000 boot/boot_img_720p.bin.gz
#   unzip    0x48000000 0x58000000
#
# so the file names and /boot on partition 2 are fixed. Partition 1 is vfat,
# which ext4load cannot read.
#
# Each file is a raw ARGB8888 frame buffer, gzip compressed:
#   boot_img_720p.bin.gz    1280x720x4  = 3686400 bytes
#   boot_img_1080p.bin.gz   1920x1080x4 = 8294400 bytes
#
# The size must match DISPLAY_MODE in drivers/video/rzg2l/rzg2l_video.c.
# U-Boot unpacks straight into the frame buffer and does not check it.
#
# For your own logo, replace the files from a bbappend (FILESEXTRAPATHS).

SUMMARY = "U-Boot still logo images for the RZ/G3L seamless still logo"
DESCRIPTION = "Installs the gzip-compressed raw frame buffer images that the \
U-Boot display driver loads from /boot on the rootfs partition and scans out \
before the kernel starts."
HOMEPAGE = "https://github.com/renesas-rz"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# unpack=0: U-Boot needs the compressed file. do_unpack would gunzip it.
SRC_URI = " \
	file://boot_img_720p.bin.gz;unpack=0 \
	file://boot_img_1080p.bin.gz;unpack=0 \
"

S = "${WORKDIR}"

INHIBIT_DEFAULT_DEPS = "1"
do_configure[noexec] = "1"
do_compile[noexec] = "1"

# The logo depends on the display of this machine, not only on the
# architecture.
PACKAGE_ARCH = "${MACHINE_ARCH}"

RZG3L_STILL_LOGO_DIR ?= "/boot"

do_install() {
	install -d ${D}${RZG3L_STILL_LOGO_DIR}
	install -m 0644 ${WORKDIR}/boot_img_720p.bin.gz  ${D}${RZG3L_STILL_LOGO_DIR}/
	install -m 0644 ${WORKDIR}/boot_img_1080p.bin.gz ${D}${RZG3L_STILL_LOGO_DIR}/
}

FILES:${PN} = "${RZG3L_STILL_LOGO_DIR}"

# Not ELF: nothing to strip, split or check for libraries.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_SYSROOT_STRIP = "1"
EXCLUDE_FROM_SHLIBS = "1"
