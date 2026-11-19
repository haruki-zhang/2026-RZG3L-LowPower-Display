SUMMARY = "RZ/G3L LowPower Display Demo - CM33 firmware (BL22)"
DESCRIPTION = "Bare-metal Cortex-M33 firmware loaded and started by BL2 \
(meta-rz-multi-os RZ_CM33_FIRMWARE_LOAD): module power measurement (ISL28022 on \
RIIC0), display hand-over on the running VSPD, DISP_RET wake-up with the DDR \
retention exit and the MHU wake-up of the CA55."
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause;md5=550794465ba0ec5312d6919e203a55f9"

COMPATIBLE_MACHINE = "smarc-rzg3l"

inherit deploy

# The DDR retention exit is compiled from the TF-A DDR driver and DDR parameters, the same
# sources BL2 is built from: keep SRCREV_tfa = TFA_REV:rzg3l-family of
# meta-rz-bsp/recipes-bsp/trusted-firmware-a/trusted-firmware-a_2.10.bb (the
# meta-rz-multi-os and demo TF-A patches do not touch the DDR driver).
SRC_URI = " \
    file://lpdemo-cm33-fw \
    git://github.com/renesas-rz/rzg_trusted-firmware-a.git;protocol=https;nobranch=1;name=tfa;destsuffix=tfa \
"
SRCREV_tfa = "c06209f12ff5d3a94e674a7f46fd01ec76356928"
SRCREV_FORMAT = "tfa"
S = "${WORKDIR}/lpdemo-cm33-fw"

# Arm GNU Toolchain (arm-none-eabi) from meta-arm-toolchain
DEPENDS = "gcc-arm-none-eabi-native"

# The firmware does not run on the CA55 and is not linked against the target libc
INHIBIT_DEFAULT_DEPS = "1"
PACKAGE_ARCH = "${MACHINE_ARCH}"

do_configure[noexec] = "1"

# e.g. LPDEMO_CM33_EXTRA_CFLAGS = "-DLPDEMO_NO_WFI" in local.conf
LPDEMO_CM33_EXTRA_CFLAGS ??= ""

# Extension linked into the firmware (src/ext.h), set by a bbappend of another layer, e.g.
# meta-rzg3l-seamless-logo-cm33-anim (boot logo animation): absolute source paths and -I options
LPDEMO_CM33_EXTRA_SRCS ??= ""
LPDEMO_CM33_EXTRA_INCLUDES ??= ""

do_compile() {
	# The Makefile sets CC/CFLAGS/LDFLAGS itself (overriding the CA55 target environment)
	oe_runmake -C ${S} CROSS_COMPILE=arm-none-eabi- TFA_DIR=${WORKDIR}/tfa \
		EXTRA_CFLAGS="${LPDEMO_CM33_EXTRA_CFLAGS}" \
		EXTRA_SRCS="${LPDEMO_CM33_EXTRA_SRCS}" \
		EXTRA_INCLUDES="${LPDEMO_CM33_EXTRA_INCLUDES}"
}

# Boot media offset read by BL2 (RZG3L_M33_FW_OFFSET): QSPI 0x270000,
# eMMC boot partition 1 sector 0x1380
do_install() {
	install -d ${D}/boot
	install -m 0644 ${S}/out/lpdemo-cm33-fw.bin ${D}/boot/lpdemo-cm33-fw.bin
}

do_deploy() {
	install -d ${DEPLOYDIR}
	for f in bin srec elf map; do
		install -m 0644 ${S}/out/lpdemo-cm33-fw.$f ${DEPLOYDIR}/lpdemo-cm33-fw-${MACHINE}.$f
	done
}
addtask deploy after do_compile before do_build

FILES:${PN} = "/boot/lpdemo-cm33-fw.bin"
INSANE_SKIP:${PN} = "arch"
