FILESEXTRAPATHS_prepend_as9737-32db := "${THISDIR}/${PN}:"
SRC_URI += " \
    file://post_boot.sh \
    "

do_install_append_as9737-32db() {
        install -d ${D}${bindir}
        install -m 0755 ${S}/post_boot.sh ${D}${bindir}/
}
