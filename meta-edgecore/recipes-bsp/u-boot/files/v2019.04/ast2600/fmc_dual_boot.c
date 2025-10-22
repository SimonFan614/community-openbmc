// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 (C) Facebook Technology Inc.
 * Copyright 2020 (C) Accton Technology Inc.
 *
 */

#include <common.h>
#include <asm/io.h>
#include <asm/arch/fmc_dual_boot_ast2600.h>
#include <command.h>

void fmc_enable_dual_boot(int timeout)
{
    /* Enable boot SPI or eMMC ABR, and enable SPI 3B/4B address mode auto-detection */
    setbits_le32(SCU_HW_STRAPPING_REG, SCU_ENABLE_ABR);
    setbits_le32(SCU_HW_STRAPPING_REG, SCU_3B_4B_AUTO_DETECTION);

    /* Enable Auto Soft-Reset Command Control*/
    setbits_le32(AUTO_SOFT_RESET_CONTROL, WAIT_SPI_WIP_IDLE);
    clrbits_le32(AUTO_SOFT_RESET_CONTROL, GENERATE_SOFT_RESET_COMMAND);

    /* Disable WDT */
    writel(WDT_DISABLE, WDT_CONTROL_STATUS_REG);

    writel(FMC_WDT_TIMEOUT * timeout, WDT_TIMER_RELOAD_REG);

    /* Restart watchdog timer register */
    writel(WDT_RESTART_MAGIC, WDT_CNT_RESTART_REG);

    /* Enable WDT */
    writel(WDT_ENABLE, WDT_CONTROL_STATUS_REG);
}

void fmc_disable_dual_boot_wtd(void)
{
    /* Disable WDT */
    writel(WDT_DISABLE, WDT_CONTROL_STATUS_REG);

    writel(AST2600_WTD_TIMEOUT, WDT_TIMER_RELOAD_REG);
}

/**
 * do_wtd_start() - Handle the "wtd start" command-line command
 * @cmdtp:      Command data struct pointer
 * @flag:       Command flag
 * @argc:       Command-line argument count
 * @argv:       Array of command-line arguments
 *
 * Returns zero always.
 */
static int do_wtd_start(cmd_tbl_t *cmdtp, int flag, int argc,
                                char * const argv[])
{
    int timeout;
    if (argc > 1) {
        timeout = simple_strtoul(argv[1], NULL, 10);
        if (timeout < 0) {
            printf("Invalid timeout %d\n", timeout);
            return -1;
        }
    }
    else{
       timeout = WDT_EC_TIMEOUT_DEFAULT;
    }
    fmc_enable_dual_boot(timeout);
    printf("\n");
    return 0;
}

/**
 * do_wtd_stop() - Handle the "wtd stop" command-line command
 * @cmdtp:      Command data struct pointer
 * @flag:       Command flag
 * @argc:       Command-line argument count
 * @argv:       Array of command-line arguments
 *
 * Returns zero always.
 */
static int do_wtd_stop(cmd_tbl_t *cmdtp, int flag, int argc,
                                char * const argv[])
{
    fmc_disable_dual_boot_wtd();
    printf("\n");
    return 0;
}

static cmd_tbl_t cmd_wtd_sub[] = {
    U_BOOT_CMD_MKENT(start, 2, 1, do_wtd_start, "", ""),
    U_BOOT_CMD_MKENT(stop, 1, 1, do_wtd_stop, "", ""),
};

/**
 * do_ec_watchdog() - Handle the "wtd" command-line command
 * @cmdtp:      Command data struct pointer
 * @flag:       Command flag
 * @argc:       Command-line argument count
 * @argv:       Array of command-line arguments
 *
 * Returns zero on success, CMD_RET_USAGE in case of misuse and negative
 * on error.
 */
static int do_ec_watchdog(cmd_tbl_t * cmdtp, int flag, int argc, char * const argv[])
{
    cmd_tbl_t *c;

    if (argc > 3)
        return CMD_RET_USAGE;

    /* Strip off leading 'wtd' command argument */
    argc--;
    argv++;

    c = find_cmd_tbl(argv[0], &cmd_wtd_sub[0], ARRAY_SIZE(cmd_wtd_sub));

    if (c)
        return c->cmd(cmdtp, flag, argc, argv);
    else
        return CMD_RET_USAGE;
}

#ifdef CONFIG_SYS_LONGHELP
static char watchdog_help_text[] =
    "wtd start [timeout] - enable watchdog, default timeout is 5 min\n"
    "wtd stop - disable watchdog";
#endif

U_BOOT_CMD(
    wtd, 3, 0, do_ec_watchdog,
        "Watchdog sub-system",
        watchdog_help_text
);
