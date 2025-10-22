FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"
SRC_URI_append = " file://aspeed-g6.dtsi \
                   file://ftgmac100.c \
                   file://ftgmac100.h \
                   file://spi-nor.c \
                   file://tps53679.c \
                   file://tps546d24.c \
                   file://ec_proc.c \
                   file://ec_proc_fs.h \
                   file://jtag \
                   file://include \
                   file://0001-modify-ncsi-temp-workaround.patch \
                   file://0002-aspeed-watchdog-add-timeout-property-in-dvt.patch \
                   file://0003-1.-remove-check-action-for-vout_mode.-2.-Retry-to-get-vout_mode.patch \
                   file://0004-work-around-spi-probe-fail.patch \
                   file://0005-drivers-Kconfig-Makefile-Add-jtag-master-driver.patch \
                   file://0007-ignore-irq-mask-mismatch-messages.patch \
                   file://0008-Change-cputemp-retry-to-workque.patch \
                   file://0009-Add-of_property-to-configure-enable-extended-temp-ra.patch \
                   file://0010-lpc-snooper-add-filter-to-drop-duplicate-postcode.patch \
                   file://0011-add-NCSI-OEM-COMMAND-TO-KEEP-PHY-UP.patch \
                   file://0012-bios-not-probe-in-startup.patch \
                   file://0013-Add-edge_proc_mode-sysfs.patch \
                   file://0014-Add-tps546d24-pmbus-driver.patch \
                   file://0015-pmbus-vid-mode-add-VR12-VR13-versions.patch \
                 "

do_kernel_prepare () {
  cp -rf ${WORKDIR}/aspeed-g6.dtsi ${STAGING_KERNEL_DIR}/arch/${ARCH}/boot/dts/aspeed-g6.dtsi
  cp -rf ${WORKDIR}/spi-nor.c ${STAGING_KERNEL_DIR}/drivers/mtd/spi-nor/spi-nor.c
  cp -rf ${WORKDIR}/tps53679.c ${STAGING_KERNEL_DIR}/drivers/hwmon/pmbus/tps53679.c
  cp -rf ${WORKDIR}/tps546d24.c ${STAGING_KERNEL_DIR}/drivers/hwmon/pmbus/tps546d24.c
  cp -rf ${WORKDIR}/ec_proc.c ${STAGING_KERNEL_DIR}/fs/proc/ec_proc.c
  cp -rf ${WORKDIR}/ec_proc_fs.h ${STAGING_KERNEL_DIR}/include/linux/ec_proc_fs.h
  cp -rf ${WORKDIR}/ftgmac100.c ${STAGING_KERNEL_DIR}/drivers/net/ethernet/faraday
  cp -rf ${WORKDIR}/ftgmac100.h ${STAGING_KERNEL_DIR}/drivers/net/ethernet/faraday
  cp -rf ${WORKDIR}/jtag ${STAGING_KERNEL_DIR}/drivers
  cp -rf ${WORKDIR}/include ${STAGING_KERNEL_DIR}
}
addtask do_kernel_prepare after do_patch before do_configure
