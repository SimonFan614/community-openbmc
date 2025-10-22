// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for TEXAS TPS546D24 buck converter
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pmbus.h>
#include "pmbus.h"

static int tps546d24_identify(struct i2c_client *client,
                             struct pmbus_driver_info *info)
{
	int ret;

	/* Read the register with VOUT scaling value.*/
	ret = pmbus_read_byte_data(client, 0, PMBUS_VOUT_MODE);
	if (ret < 0)
		return ret;

	return 0;
}

static struct pmbus_driver_info tps546d24_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN
			| PMBUS_HAVE_IOUT | PMBUS_HAVE_VOUT
			| PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_STATUS_VOUT
			| PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
	.identify = tps546d24_identify,
};

static int tps546d24_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int reg;
	struct pmbus_driver_info *info;

	info = devm_kmemdup(&client->dev, &tps546d24_info, sizeof(*info), GFP_KERNEL);

	return pmbus_do_probe(client, id, info);
}

static const struct i2c_device_id tps546d24_id[] = {
	{"tps546d24", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, tps546d24_id);

static const struct of_device_id __maybe_unused tps546d24_of_match[] = {
	{.compatible = "ti,tps546d24"},
	{}
};
MODULE_DEVICE_TABLE(of, tps546d24_of_match);

/* This is the driver that will be inserted */
static struct i2c_driver tps546d24_driver = {
	.driver = {
		   .name = "tps546d24",
		   .of_match_table = of_match_ptr(tps546d24_of_match),
	   },
	.probe = tps546d24_probe,
	.remove = pmbus_do_remove,
	.id_table = tps546d24_id,
};

module_i2c_driver(tps546d24_driver);

MODULE_AUTHOR("Duke Du <dukedu83@gmail.com>");
MODULE_DESCRIPTION("PMBus driver for TI tps546d24");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
