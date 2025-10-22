FILESEXTRAPATHS_prepend_as9737-32db := "${THISDIR}/files:"

SRC_URI_prepend_as9737-32db = "file://as9737-32db.cfg \
              file://aspeed-common.h \
              file://ipmi-handler.c \
              file://0001-Add-ec-as9737-32db_dts.patch \
              file://dts/ast2600-ec-as9737-32db.dts \
             "

do_compile_prepend_as9737-32db () {
  cp -rf ${WORKDIR}/dts/ast2600-ec-as9737-32db.dts ${S}/arch/arm/dts/ast2600-ec-as9737-32db.dts
  cp -rf ${WORKDIR}/aspeed-common.h ${S}/include/configs/aspeed-common.h
  cp -rf ${WORKDIR}/ipmi-handler.c ${AST2600A1_EVB_BOARD_PATH}
}
