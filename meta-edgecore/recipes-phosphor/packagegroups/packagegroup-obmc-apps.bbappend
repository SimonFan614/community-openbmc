RDEPENDS_${PN}-extras_append = " phosphor-webui \
                                 opkg"

RDEPENDS_${PN}-extras_remove = "obmc-ikvm"
RDEPENDS_${PN}-fan-control_remove += "phosphor-fan-monitor phosphor-fan-control"
RDEPENDS_${PN}-inventory_remove += "phosphor-fan-presence-tach"
