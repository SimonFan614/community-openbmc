/*
 * ec_sysled_cpld.h - The led driver header file for Edge-core system cpld.
 *
 * Copyright 2023-present Edge-core. All Rights Reserved.
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

#include <linux/leds.h>
#include <linux/of.h>
#include <linux/of_device.h>

#define EC_CPLD_BLINK_RATE_MS  250

enum cpld_systemleds_index {
    cpld_led_psu1 = 0,
    cpld_led_psu2,
    cpld_led_fan,
    cpld_led_location,
    cpld_led_diag,
    cpld_leds_num
};
#define NUMBER_OF_SYSTEM_LEDS  cpld_leds_num

#define EC_LEDS_SHUTDOWN    1
#define EC_LEDS_READ_ONLY   2
#define EC_LEDS_NO_TIMER    4

struct cpld_system_led {
    struct i2c_client *client;
    char name[12];
    u8 id;
    u8 ledbit;
    u8 ledmask;
    u32 reg_val;
    u32 ec_leds_flag;
    enum led_brightness  color_shutdown;
    enum led_brightness  color;
    enum led_brightness  color_display;
    struct led_classdev ldev;
};
#define ldev_to_systemled(c)       container_of(c, struct cpld_system_led, ldev)

#define CPLD_LED_BRIGHT_MAX     7

struct ec_sysled_cpld_data {
    struct i2c_client *client;
    struct mutex update_lock;

    char valid;                     /* !=0 if following fields are valid */
    unsigned long last_updated;     /* In jiffies */
    struct cpld_system_led system_leds[NUMBER_OF_SYSTEM_LEDS];
};

#define LED_SYSCPLD_REG_BIT(x, y)              ((y) << (x))
