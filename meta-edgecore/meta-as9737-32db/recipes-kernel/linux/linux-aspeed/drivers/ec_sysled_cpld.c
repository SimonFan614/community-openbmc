/*
 * ec_sysled_fpga.c - Linux kernel modules for LEDS management
 *
 * Copyright (C) 2023-2029 Simon Fan <simon_fan@edge-core.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/ec_proc_fs.h>
#include "ec_sysled_cpld.h"

static int check_same_value(int setval, s32 reg_val, int bit, int mask)
{
    if (( reg_val & LED_SYSCPLD_REG_BIT(bit, mask)) == LED_SYSCPLD_REG_BIT(bit, setval) ){
        return 1;
    }

    return 0;
}

static s32 set_reg_value(int setval, s32 reg_val, u8 bit, u8 mask)
{
    reg_val &= ~(LED_SYSCPLD_REG_BIT(bit, mask));
    reg_val |= LED_SYSCPLD_REG_BIT(bit, setval);

    return reg_val;
}

static int cpld_led_set_color(struct i2c_client *client, struct ec_sysled_cpld_data *data,
                              struct cpld_system_led *led)
{
    u8 value, conv_val;

    mutex_lock(&data->update_lock);
    conv_val = led->color;
    value = i2c_smbus_read_byte_data(client, led->reg_val);

    if( (check_same_value(conv_val, value, led->ledbit, led->ledmask)) == 1 ) {
        mutex_unlock(&data->update_lock);
        return 1;
    }

    i2c_smbus_write_byte_data(client, led->reg_val,
            set_reg_value(conv_val & led->ledmask, value, led->ledbit, led->ledmask));
    mutex_unlock(&data->update_lock);

    return 0;
}

static int systemled_set_brightness(struct led_classdev *led_cdev,
        enum led_brightness value)
{
    struct cpld_system_led *led = ldev_to_systemled(led_cdev);
    struct i2c_client *client;
    struct ec_sysled_cpld_data *data;

    if( led->ec_leds_flag & EC_LEDS_READ_ONLY ){
        return -EINVAL;
    }

    if( value > CPLD_LED_BRIGHT_MAX ){
        return -EINVAL;
    }

    client = led->client;
    data = i2c_get_clientdata(client);
    led->color = value & led->ledmask;

    if ( !test_bit(LED_BLINK_SW, &led_cdev->work_flags) ) {
        led->color_display = led->color;
    }
    else if( (led->color != 0) && (led->color_display != led->color) ) {
        led->color_display = led->color;
    }

    cpld_led_set_color(client, data, led);
    return 0;
}

static int cpld_led_get_color(struct i2c_client *client, struct cpld_system_led *led)
{
    u8 value;

    value = i2c_smbus_read_byte_data(client, led->reg_val);
    led->color = led->color_display = (value >> led->ledbit) & led->ledmask;

    return led->color;
}

static enum led_brightness systemled_get_brightness(struct led_classdev *led_cdev)
{
    struct cpld_system_led *led = ldev_to_systemled(led_cdev);
    struct i2c_client *client;

    client = led->client;
    if( led->ec_leds_flag & EC_LEDS_READ_ONLY ){
        return cpld_led_get_color(client, led);
    }
    else{
        return led->color_display;
    }
}

static int systemled_set_dummy_blink(struct led_classdev *led_cdev,
                                   unsigned long *delay_on,
                                   unsigned long *delay_off)
{
    return 0;
}

int cpld_system_led_configure(struct i2c_client *client, struct device_node *node,
        struct cpld_system_led sys_leds[], int led_num)
{
    int rc = 0, i = 0;
    struct ec_sysled_cpld_data *data;
    struct device_node *child;
    const char *name;
    u8 color;

    data = i2c_get_clientdata(client);
    for_each_child_of_node(node, child) {
        if (of_property_read_u32(child, "reg", &sys_leds[i].reg_val)) {
            dev_err(&client->dev,
                    "couldn't get led property reg(index:%d)\n", i);
            i++;
            continue;
        }

        if (of_property_read_string(child, "name", &name) &&
            of_property_read_string(child, "led-name", &name) ) {
            dev_err(&client->dev,
                    "couldn't get led name(index:%d, reg:%x)\n", i, sys_leds[i].reg_val);
            i++;
            continue;
        }
        strncpy(sys_leds[i].name, name, sizeof(sys_leds[i].name) - 1);
        sys_leds[i].client = client;
        sys_leds[i].id = i;

        if ( of_property_read_u8(child, "led-bit", &sys_leds[i].ledbit) ) {
            dev_err(&client->dev,
                    "couldn't get led-bit(index:%d, reg:%x, name:%s)\n", i, sys_leds[i].reg_val, name);
            i++;
            continue;
        }

        if ( of_property_read_u8(child, "led-mask", &sys_leds[i].ledmask) ) {
            sys_leds[i].ledmask = 1;
        }

        if ( !of_property_read_u8(child, "color", &color) ) {
            sys_leds[i].color = (enum led_brightness)color;
        }
        else{
            sys_leds[i].color = 0;
        }
        sys_leds[i].color_display = sys_leds[i].color;

        if ( !of_property_read_u8(child, "shutdown-color", &color) ) {
            sys_leds[i].color_shutdown = (enum led_brightness)color;
            sys_leds[i].ec_leds_flag |= EC_LEDS_SHUTDOWN;
        }
        else{
            sys_leds[i].color_shutdown = 0;
        }

        if ( of_get_property(child, "read-only", NULL)) {
            sys_leds[i].ec_leds_flag |= EC_LEDS_READ_ONLY;
            sys_leds[i].ec_leds_flag |= EC_LEDS_NO_TIMER;
        }

        if ( of_get_property(child, "no-timer", NULL)) {
            sys_leds[i].ec_leds_flag |= EC_LEDS_NO_TIMER;
        }

        sys_leds[i].ldev.name = sys_leds[i].name;
        sys_leds[i].ldev.brightness = sys_leds[i].color;

        sys_leds[i].ldev.max_brightness = CPLD_LED_BRIGHT_MAX;
        sys_leds[i].ldev.brightness_set_blocking = systemled_set_brightness;
        sys_leds[i].ldev.brightness_get = systemled_get_brightness;
        if( sys_leds[i].ec_leds_flag & EC_LEDS_NO_TIMER ){
            sys_leds[i].ldev.blink_set =  systemled_set_dummy_blink;
        }

        rc = devm_led_classdev_register(&client->dev, &sys_leds[i].ldev);
        if (rc < 0) {
            dev_err(&client->dev,
                "couldn't register LED %s, rc:%d\n", sys_leds[i].ldev.name, rc);
            return rc;
        }
        cpld_led_set_color(client, data, &sys_leds[i]);

        i++;
        if( i >= led_num ){
            break;
        }
    }

    return rc;
}

static int cpld_led_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
    struct ec_sysled_cpld_data *data;
    struct device *dev = &client->dev;

    data = devm_kzalloc(&client->dev, sizeof(struct ec_sysled_cpld_data),
                    GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    i2c_set_clientdata(client, data);
    data->client = client;
    mutex_init(&data->update_lock);

    cpld_system_led_configure(client,  dev_of_node(&client->dev), data->system_leds, NUMBER_OF_SYSTEM_LEDS);

    dev_info(dev, "%s: cpld leds registered!\n", client->name);
    return 0;
}

static void cpld_led_shutdown(struct i2c_client *client)
{
    struct ec_sysled_cpld_data *data = i2c_get_clientdata(client);
    struct cpld_system_led *sys_leds = data->system_leds;
    int i = 0;
    for( i=0; i<NUMBER_OF_SYSTEM_LEDS; i++ )
    {
        if( sys_leds->ec_leds_flag & EC_LEDS_SHUTDOWN )
        {
            systemled_set_brightness(&sys_leds->ldev, sys_leds->color_shutdown);
        }
        sys_leds++;
    }
}

#ifdef CONFIG_OF
static const struct of_device_id of_cpld_leds_match[] = {
    { .compatible = "edge-core,ec_system_led" },
    {},
};

MODULE_DEVICE_TABLE(of, of_cpld_leds_match);
#endif

static const struct i2c_device_id cpld_led_id[] = {
    { "ec_sysled_cpld", 0 },
    { }
};

MODULE_DEVICE_TABLE(i2c, cpld_led_id);

static struct i2c_driver cpld_led_driver = {
    .driver = {
        .name = "leds-ec-cpld",
        .of_match_table = of_match_ptr(of_cpld_leds_match),
    },
    .probe = cpld_led_probe,
    .shutdown = cpld_led_shutdown,
    .id_table = cpld_led_id,
};

module_i2c_driver(cpld_led_driver);

MODULE_AUTHOR("Simon Fan");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPLD System LED Controller");
