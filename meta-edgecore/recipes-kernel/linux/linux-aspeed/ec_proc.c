// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>

static int led_ctrl_mode = 1;

int get_led_ctrl_mode(void)
{
	return led_ctrl_mode;
}
EXPORT_SYMBOL(get_led_ctrl_mode);

static struct ctl_table led_table[] = {
	{
		.procname       = "led_ctrl_mode",
		.data           = &led_ctrl_mode,
		.maxlen         = sizeof(led_ctrl_mode),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
		.extra1         = SYSCTL_ZERO,
		.extra2         = SYSCTL_ONE,
	},
	{ }
};

static struct ctl_table led_dir_table[] = {
	{
		.procname       = "edgecore",
		.mode           = 0555,
		.child          = led_table,
	},
	{ }
};

static struct ctl_table led_root_table[] = {
	{
		.procname       = "dev",
		.mode           = 0555,
		.child          = led_dir_table,
	},
	{ }
};

static int __init led_sysctl_init(void)
{
	register_sysctl_table(led_root_table);
	return 0;
}
fs_initcall(led_sysctl_init);
