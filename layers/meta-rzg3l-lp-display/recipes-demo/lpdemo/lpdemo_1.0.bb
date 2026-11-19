SUMMARY = "RZ/G3L LowPower Display Demo - Linux side tools and GUI"
DESCRIPTION = "lpdemo-ctl (CM33 control block access, RIIC0 hand-over) and the GTK3 demo GUI."
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause;md5=550794465ba0ec5312d6919e203a55f9"

SRC_URI = " \
    file://lpdemo_shm.py \
    file://lpdemo_lpimage.py \
    file://lpdemo_normal.py \
    file://renesas-wordmark-240x40.png \
    file://lpdemo-ctl \
    file://lpdemo-gui \
    file://lpdemo-cm33-grant.service \
    file://lpdemo-gui.service \
    file://lpdemo-modprobe.conf \
    file://lpdemo-modules-load.conf \
"
S = "${WORKDIR}"

inherit allarch systemd python3-dir

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
	install -d ${D}${PYTHON_SITEPACKAGES_DIR}
	install -m 0644 ${S}/lpdemo_shm.py ${D}${PYTHON_SITEPACKAGES_DIR}/
	install -m 0644 ${S}/lpdemo_lpimage.py ${D}${PYTHON_SITEPACKAGES_DIR}/
	install -m 0644 ${S}/lpdemo_normal.py ${D}${PYTHON_SITEPACKAGES_DIR}/

	# Renesas wordmark, pre-scaled to the 240x40 of the GUI (handoff section 6.1)
	install -d ${D}${datadir}/lpdemo
	install -m 0644 ${S}/renesas-wordmark-240x40.png ${D}${datadir}/lpdemo/

	install -d ${D}${bindir}
	install -m 0755 ${S}/lpdemo-ctl ${D}${bindir}/
	install -m 0755 ${S}/lpdemo-gui ${D}${bindir}/

	install -d ${D}${systemd_system_unitdir}
	install -m 0644 ${S}/lpdemo-cm33-grant.service ${D}${systemd_system_unitdir}/
	install -m 0644 ${S}/lpdemo-gui.service ${D}${systemd_system_unitdir}/

	install -d ${D}${sysconfdir}/modprobe.d ${D}${sysconfdir}/modules-load.d
	install -m 0644 ${S}/lpdemo-modprobe.conf ${D}${sysconfdir}/modprobe.d/lpdemo.conf
	install -m 0644 ${S}/lpdemo-modules-load.conf ${D}${sysconfdir}/modules-load.d/lpdemo.conf
}

PACKAGES = "lpdemo-ctl lpdemo-gui"

FILES:lpdemo-ctl = " \
    ${PYTHON_SITEPACKAGES_DIR}/lpdemo_shm.py \
    ${PYTHON_SITEPACKAGES_DIR}/lpdemo_lpimage.py \
    ${bindir}/lpdemo-ctl \
    ${systemd_system_unitdir}/lpdemo-cm33-grant.service \
    ${sysconfdir}/modprobe.d/lpdemo.conf \
    ${sysconfdir}/modules-load.d/lpdemo.conf \
"
# LP layers (lpdemo_lpimage.py): pycairo + PangoCairo through PyGObject, DejaVu only
# (design_handoff_lpdemo_gui section 4)
RDEPENDS:lpdemo-ctl = "python3-core python3-mmap python3-json python3-pycairo \
    python3-pygobject pango ttf-dejavu-sans ttf-dejavu-sans-condensed"

FILES:lpdemo-gui = " \
    ${bindir}/lpdemo-gui \
    ${PYTHON_SITEPACKAGES_DIR}/lpdemo_normal.py \
    ${datadir}/lpdemo \
    ${systemd_system_unitdir}/lpdemo-gui.service \
"
RDEPENDS:lpdemo-gui = "lpdemo-ctl python3-pygobject gtk+3 weston-init"

SYSTEMD_PACKAGES = "lpdemo-ctl lpdemo-gui"
SYSTEMD_SERVICE:lpdemo-ctl = "lpdemo-cm33-grant.service"
SYSTEMD_SERVICE:lpdemo-gui = "lpdemo-gui.service"
