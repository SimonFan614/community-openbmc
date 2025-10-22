SUMMARY = "Edge-Core OpenBMC Post Boot"
PR = "r1"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

inherit obmc-phosphor-systemd

S = "${WORKDIR}"

RDEPENDS_${PN} += "bash"

SRC_URI += " \
    file://finish_boot.sh \
    "

do_install() {
        install -d ${D}${bindir}
        install -m 0755 ${S}/finish_boot.sh ${D}${bindir}/finish_boot.sh
}

POST_BOOT_SRV = "post-boot.service"
FINISH_BOOT_SRV = "finish-boot.service"
SYSTEMD_SERVICE_${PN} += "${POST_BOOT_SRV}"
SYSTEMD_SERVICE_${PN} += "${FINISH_BOOT_SRV}"
