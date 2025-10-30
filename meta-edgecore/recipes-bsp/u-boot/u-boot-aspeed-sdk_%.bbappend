FILESEXTRAPATHS_prepend := "${THISDIR}/files/v2019.04:"

SRC_URI_append = " \
                   file://0003-add-w25q512jve.patch \
                   file://0004-u-boot-ast2600-Add-FMC-dual-boot-config.patch \
                   file://0006-Enable-FMC-watchdog-timer-to-switch-to-2nd-flash-if-.patch \
                   file://0008-add-i2c-mux-command-and-api-prepare_eeprom.patch \
                   file://0009-read-eeprom-as-mac-address.patch \
                   file://0010-Add-fru-eeprom-parsing.patch \
                   file://0011-read-bmc-eeprom.patch \
                   file://0012-uboot-v2019.04-arch-arm-lib-interrupt-Use-the-weak-f.patch \
                   file://0013-uboot-v2019.04-arm-arch-lib-Enable-interrupt.patch \
                   file://0014-u-boot-v2019.04-board-aspeed-evb_ast2600a1-Add-ast-k.patch \
                   file://0015-ncsi-add-packet-analyzer.patch \
                   file://0016-work-around-gls-error.patch \
                   file://0017-fix-ncsi-initial-state-error.patch \
                   file://0018-send-gls-if-state-config-when-get-aen.patch \
                   file://0019-ncsi-use-kernel-initiator.patch \
                   file://platform.h \
                   file://fmc_dual_boot_ast2600.h \
                   file://otp_info.h \
                   file://fru_parsing.h \
                   file://common.cfg \
                   file://ast2600/platform.S \
                   file://ast2600/scu_info.c \
                   file://ast2600/board_common.c \
                   file://ast2600/fmc_dual_boot.c \
                   file://ast2600/clk_ast2600.c \
                   file://ast2600/otp.c \
                   file://ast2600/fru_parsing.c \
                   file://ast2600/ast-kcs.c \
                   file://ast2600/ast-kcs.h \
                   file://ast2600/ast-irq.c \
                   file://ast2600/ast-timer.c \
                   file://ast2600/ipmi-handler.h \
                   file://ast2600/evb_ast2600a1.c \
                 "

AST2600_FILE_DIR="${WORKDIR}/ast2600"
AST2600_MACH_PATH="${S}/arch/arm/mach-aspeed/ast2600"
AST2600A1_EVB_BOARD_PATH="${S}/board/aspeed/evb_ast2600a1"
ASPEED_CLK_PATH="${S}/drivers/clk/aspeed"
ASPEED_INCLUDE_PATH="${S}/arch/arm/include/asm/arch-aspeed"
ASPEED_CMD_PATH="${S}/cmd"

do_uboot_prepare () {
    cp -rfv ${AST2600_FILE_DIR}/platform.S ${AST2600_MACH_PATH}
    cp -rfv ${AST2600_FILE_DIR}/scu_info.c ${AST2600_MACH_PATH}
    cp -rfv ${AST2600_FILE_DIR}/board_common.c ${AST2600_MACH_PATH}
    cp -rfv ${AST2600_FILE_DIR}/fmc_dual_boot.c ${AST2600_MACH_PATH}
    cp -rfv ${AST2600_FILE_DIR}/clk_ast2600.c ${ASPEED_CLK_PATH}
    cp -rfv ${AST2600_FILE_DIR}/otp.c ${ASPEED_CMD_PATH}
    cp -rfv ${AST2600_FILE_DIR}/fru_parsing.c ${AST2600_MACH_PATH}
    cp -rfv ${WORKDIR}/platform.h ${ASPEED_INCLUDE_PATH}
    cp -rfv ${WORKDIR}/fmc_dual_boot_ast2600.h ${ASPEED_INCLUDE_PATH}
    cp -rfv ${WORKDIR}/otp_info.h ${ASPEED_CMD_PATH}
    cp -rfv ${WORKDIR}/fru_parsing.h ${ASPEED_INCLUDE_PATH}
    cp -rfv ${AST2600_FILE_DIR}/ast-kcs.c ${AST2600A1_EVB_BOARD_PATH}
    cp -rfv ${AST2600_FILE_DIR}/ast-kcs.h ${AST2600A1_EVB_BOARD_PATH}
    cp -rfv ${AST2600_FILE_DIR}/ast-irq.c ${AST2600A1_EVB_BOARD_PATH}
    cp -rfv ${AST2600_FILE_DIR}/ast-timer.c ${AST2600A1_EVB_BOARD_PATH}
    cp -rfv ${AST2600_FILE_DIR}/ipmi-handler.h ${AST2600A1_EVB_BOARD_PATH}
    cp -rfv ${AST2600_FILE_DIR}/evb_ast2600a1.c ${AST2600A1_EVB_BOARD_PATH}
}
addtask do_uboot_prepare after do_patch before do_configure
