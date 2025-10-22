FILESEXTRAPATHS_prepend_as9737-32db := "${THISDIR}/${PN}:"
SRC_URI_append_as9737-32db = " \
            file://as9737-32db.cfg \
            file://0001-add-fpga-i2c-mux-drivers.patch \
            file://0002-change-i2c-bus-drivers_autoprobe-to-0.patch \
            file://0003-add-EC-fan-system-cpld-drivers.patch \
            file://0004-Add-ec_common_psu-driver.patch \
            file://0009-Add-com-e-ec-driver.patch \
            file://0010-Add-fpga-system-led-drivers.patch \
            file://dts/aspeed-bmc-ec-as9737-32db.dts \
            file://drivers/ec_fpga_mux.c \
            file://drivers/ec_fan_cpld.c \
            file://drivers/ec_common_psu.c \
            file://drivers/ec_system_fpga.c \
            file://drivers/ec_com_e_drv.c \
            file://drivers/ec_sysled_cpld.c \
            file://drivers/ec_sysled_cpld.h \
          "

do_compile_prepend_as9737-32db () {
  cp -rf ${WORKDIR}/dts/aspeed-bmc-ec-as9737-32db.dts ${STAGING_KERNEL_DIR}/arch/${ARCH}/boot/dts/aspeed-bmc-ec-as9737-32db.dts
  cp -rf ${WORKDIR}/drivers/ec_fan_cpld.c ${STAGING_KERNEL_DIR}/drivers/hwmon/ec_fan_cpld.c
  cp -rf ${WORKDIR}/drivers/ec_fpga_mux.c ${STAGING_KERNEL_DIR}/drivers/hwmon/ec_fpga_mux.c
  cp -rf ${WORKDIR}/drivers/ec_system_fpga.c ${STAGING_KERNEL_DIR}/drivers/hwmon/ec_system_fpga.c
  cp -rf ${WORKDIR}/drivers/ec_com_e_drv.c ${STAGING_KERNEL_DIR}/drivers/hwmon/ec_com_e_drv.c
  cp -rf ${WORKDIR}/drivers/ec_sysled_cpld.c ${STAGING_KERNEL_DIR}/drivers/hwmon/ec_sysled_cpld.c
  cp -rf ${WORKDIR}/drivers/ec_sysled_cpld.h ${STAGING_KERNEL_DIR}/drivers/hwmon/ec_sysled_cpld.h
  cp -rf ${WORKDIR}/drivers/ec_common_psu.c ${STAGING_KERNEL_DIR}/drivers/hwmon/pmbus/ec_common_psu.c
}
