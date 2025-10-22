/*
 * An hwmon driver for Common Module PSU in Edge-core Inc.
 * ec_common_psu.c - Support for ACBEL FSH082 Power Supply Module
 *
 * Copyright (C) 2016 Edge-core Network Technology Corporation
 *
 * Michael <michael_shih@edge-core.com>
 *
 * Based on ym2651y.c
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
 * Foundation, Inc.,
 * NO. 1, CREATION RD. III, HSINCHU SCIENCE PARK, HSINCHU, TAIWAN, R.O.C.
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/version.h>

#define MAX_FAN_DUTY_CYCLE      100
#define I2C_RW_RETRY_COUNT      10
#define I2C_RW_RETRY_INTERVAL   60 /* ms */

#define THRESHOLD_PARAM_VIN  0
#define THRESHOLD_PARAM_FAN  1
#define THRESHOLD_PARAM_TEMP1  2
#define THRESHOLD_PARAM_TEMP2  3
#define THRESHOLD_PARAM_TEMP3  4

static int support_i2c_block = 1; // 1: support I2C_FUNC_SMBUS_I2C_BLOCK 0: not support

/* Addresses scanned
 */
static const unsigned short normal_i2c[] = { I2C_CLIENT_END };

/* Each client has this additional data
 */
struct ec_psu_data {
    struct mutex        update_lock;
    struct i2c_client *client;
    char                valid;          /* !=0 if registers are valid */
    unsigned long      last_updated;    /* In jiffies */
    u8   chip;               /* chip id */
    u8   capability;         /* Register value */
    u16  status_word;        /* Register value */
    u8   fan_fault;          /* Register value */
    u8   over_temp;          /* Register value */
    u16  v_in;               /* Register value */
    u16  i_in;               /* Register value */
    u16  p_in;               /* Register value */
    u16  v_out;              /* Register value */
    u16  i_out;              /* Register value */
    u16  p_out;              /* Register value */
    u8   vout_mode;          /* Register value */
    u16  temp1;              /* Register value */
    u16  temp2;              /* Register value */
    u16  temp3;              /* Register value */
    u16  fan_speed;          /* Register value */
    u16  fan_duty_cycle[2];  /* Register value */
    u8   fan_dir[5];         /* Register value */
    u8   pmbus_revision;     /* Register value */
    u8   mfr_id[20];         /* Register value */
    u8   mfr_model[24];      /* Register value */
    u8   mfr_date[6];        /* Register value */
    u8   mfr_revsion[8];     /* Register value */
    u8   mfr_serial[32];     /* Register value */
    u16  mfr_vin_min;        /* Register value */
    u16  mfr_vin_max;        /* Register value */
    u16  mfr_iin_max;        /* Register value */
    u16  mfr_iout_max;       /* Register value */
    u16  mfr_pin_max;        /* Register value */
    u16  mfr_pout_max;       /* Register value */
    u16  mfr_vout_min;       /* Register value */
    u16  mfr_vout_max;       /* Register value */
    u16  psu_image_version;  /* Register value */
    u8   line_status;        /* Register value */
    u8   line_status_old;
    u8   psu_type;           /* Register value */
    u16  v_in_hi;            /* Attribute value */
    u16  v_in_lo;            /* Attribute value */
    u16  v_in_hi_crit;       /* Attribute value */
    u16  v_in_lo_crit;       /* Attribute value */
    u16  fan_hi;             /* Attribute value */
    u16  fan_lo;             /* Attribute value */
    u16  fan_crit;           /* Attribute value */
    u16  fan_lcrit;          /* Attribute value */
    u16  i_in_hi_crit;       /* Attribute value */
    u16  i_out_hi_crit;      /* Attribute value */
    u16  p_in_hi_crit;       /* Attribute value */
    u16  p_out_hi_crit;       /* Attribute value */
    u16  temp1_max;          /* Attribute value */
    u16  temp1_min;          /* Attribute value */
    u16  temp1_crit;         /* Attribute value */
    u16  temp1_lcrit;        /* Attribute value */
    u16  temp2_max;          /* Attribute value */
    u16  temp2_min;          /* Attribute value */
    u16  temp2_crit;         /* Attribute value */
    u16  temp2_lcrit;        /* Attribute value */
    u16  temp3_max;          /* Attribute value */
    u16  temp3_min;          /* Attribute value */
    u16  temp3_crit;         /* Attribute value */
    u16  temp3_lcrit;        /* Attribute value */
    u16  v_out_hi_crit;       /* Attribute value */
    u16  v_out_lo_crit;       /* Attribute value */
    char     power_good;          /* !=0 if power good is 0, register values of sensors return 0 */
};

static ssize_t show_byte(struct device *dev, struct device_attribute *da,
             char *buf);
static ssize_t show_word(struct device *dev, struct device_attribute *da,
             char *buf);
static ssize_t show_linear(struct device *dev, struct device_attribute *da,
             char *buf);
static ssize_t show_vout(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t show_fan_fault(struct device *dev, struct device_attribute *da,
             char *buf);
static ssize_t show_over_temp(struct device *dev, struct device_attribute *da,
             char *buf);
static ssize_t show_ascii(struct device *dev, struct device_attribute *da,
             char *buf);
static ssize_t show_predefine_threshold(struct device *dev, struct device_attribute *da,
             char *buf);static ssize_t show_ascii(struct device *dev, struct device_attribute *da,
             char *buf);
static struct ec_psu_data *ec_psu_update_device(struct device *dev);
static ssize_t set_fan_duty_cycle(struct device *dev, struct device_attribute *da,
             const char *buf, size_t count);
static int ec_psu_write_word(struct i2c_client *client, u8 reg, u16 value);
static int ec_psu_read_block(struct i2c_client *client, u8 command, u8 *data, int data_len);

enum ec_psu_sysfs_attributes {
    PSU_POWER_ON = 0,
    PSU_TEMP_FAULT,
    PSU_POWER_GOOD,
    PSU_VOLT_OVER,
    PSU_CURR_OVER,
    PSU_STATUS_WORD,
    PSU_FAN1_FAULT,
    PSU_FAN_DIRECTION,
    PSU_OVER_TEMP,
    PSU_V_IN,
    PSU_I_IN,
    PSU_P_IN,
    PSU_V_OUT,
    PSU_I_OUT,
    PSU_P_OUT,
    PSU_VOUT_MODE,
    PSU_TEMP1_INPUT,
    PSU_TEMP2_INPUT,
    PSU_TEMP3_INPUT,
    PSU_FAN1_SPEED,
    PSU_FAN1_DUTY_CYCLE,
    PSU_PMBUS_REVISION,
    PSU_MFR_ID,
    PSU_MFR_MODEL,
    PSU_MFR_REVISION,
    PSU_MFR_DATE,
    PSU_MFR_SERIAL,
    PSU_MFR_VIN_MIN,
    PSU_MFR_VIN_MAX,
    PSU_MFR_VOUT_MIN,
    PSU_MFR_VOUT_MAX,
    PSU_MFR_IIN_MAX,
    PSU_MFR_IOUT_MAX,
    PSU_MFR_PIN_MAX,
    PSU_MFR_POUT_MAX,
    PSU_RAW_V_IN,
    PSU_RAW_V_OUT,
    PSU_RAW_I_OUT,
    PSU_RAW_P_OUT,
    PSU_RAW_FAN1_SPEED,
    PSU_RAW_TEMP1_INPUT,
    PSU_RAW_TEMP2_INPUT,
    PSU_RAW_TEMP3_INPUT,
    PSU_IMAGE_VERSION,
    PSU_LINE_STATUS,
    PSU_TYPE_DC_AC,
    PSU_VIN_WARN_HI,
    PSU_VIN_WARN_LO,
    PSU_VIN_CRIT_HI,
    PSU_VIN_CRIT_LO,
    PSU_FAN_WARN_HI,
    PSU_FAN_WARN_LO,
    PSU_FAN_CRIT_HI,
    PSU_FAN_CRIT_LO,
    PSU_IIN_CRIT_HI,
    PSU_IOUT_CRIT_HI,
    PSU_PIN_CRIT_HI,
    PSU_POUT_CRIT_HI,
    PSU_TEMP1_WARN_HI,
    PSU_TEMP1_WARN_LO,
    PSU_TEMP1_CRIT_HI,
    PSU_TEMP1_CRIT_LO,
    PSU_TEMP2_WARN_HI,
    PSU_TEMP2_WARN_LO,
    PSU_TEMP2_CRIT_HI,
    PSU_TEMP2_CRIT_LO,
    PSU_TEMP3_WARN_HI,
    PSU_TEMP3_WARN_LO,
    PSU_TEMP3_CRIT_HI,
    PSU_TEMP3_CRIT_LO,
    PSU_VOUT_CRIT_HI,
    PSU_VOUT_CRIT_LO
};

/* sysfs attributes for hwmon
 */
static SENSOR_DEVICE_ATTR(psu_power_on,     S_IRUGO, show_word,   NULL, PSU_POWER_ON);
static SENSOR_DEVICE_ATTR(psu_temp_fault,   S_IRUGO, show_word,   NULL, PSU_TEMP_FAULT);
static SENSOR_DEVICE_ATTR(psu_power_good,   S_IRUGO, show_word,   NULL, PSU_POWER_GOOD);
static SENSOR_DEVICE_ATTR(psu_volt_over,   S_IRUGO, show_word,   NULL, PSU_VOLT_OVER);
static SENSOR_DEVICE_ATTR(psu_curr_over,   S_IRUGO, show_word,   NULL, PSU_CURR_OVER);
static SENSOR_DEVICE_ATTR(psu_status_word,   S_IRUGO, show_word,   NULL, PSU_STATUS_WORD);
static SENSOR_DEVICE_ATTR(psu_fan1_fault,   S_IRUGO, show_fan_fault, NULL, PSU_FAN1_FAULT);
static SENSOR_DEVICE_ATTR(psu_over_temp,    S_IRUGO, show_over_temp, NULL, PSU_OVER_TEMP);
static SENSOR_DEVICE_ATTR(psu_v_in,     S_IRUGO, show_linear,   NULL, PSU_V_IN);
static SENSOR_DEVICE_ATTR(psu_i_in,     S_IRUGO, show_linear,   NULL, PSU_I_IN);
static SENSOR_DEVICE_ATTR(psu_p_in,     S_IRUGO, show_linear,   NULL, PSU_P_IN);
static SENSOR_DEVICE_ATTR(psu_v_out,        S_IRUGO, show_vout,     NULL, PSU_V_OUT);
static SENSOR_DEVICE_ATTR(psu_i_out,        S_IRUGO, show_linear,   NULL, PSU_I_OUT);
static SENSOR_DEVICE_ATTR(psu_p_out,        S_IRUGO, show_linear,   NULL, PSU_P_OUT);
static SENSOR_DEVICE_ATTR(psu_out_mode,        S_IRUGO, show_linear,   NULL, PSU_VOUT_MODE);
static SENSOR_DEVICE_ATTR(psu_temp1_input,  S_IRUGO, show_linear,   NULL, PSU_TEMP1_INPUT);
static SENSOR_DEVICE_ATTR(psu_temp2_input,  S_IRUGO, show_linear,   NULL, PSU_TEMP2_INPUT);
static SENSOR_DEVICE_ATTR(psu_temp3_input,  S_IRUGO, show_linear,   NULL, PSU_TEMP3_INPUT);
static SENSOR_DEVICE_ATTR(psu_fan1_speed_rpm, S_IRUGO, show_linear, NULL, PSU_FAN1_SPEED);
static SENSOR_DEVICE_ATTR(psu_fan1_duty_cycle_percentage, S_IWUSR | S_IRUGO, show_linear, set_fan_duty_cycle, PSU_FAN1_DUTY_CYCLE);
static SENSOR_DEVICE_ATTR(psu_fan_dir,       S_IRUGO, show_ascii,    NULL, PSU_FAN_DIRECTION);
static SENSOR_DEVICE_ATTR(psu_pmbus_revision,S_IRUGO, show_byte,   NULL, PSU_PMBUS_REVISION);
static SENSOR_DEVICE_ATTR(psu_mfr_id,       S_IRUGO, show_ascii,  NULL, PSU_MFR_ID);
static SENSOR_DEVICE_ATTR(psu_mfr_model,    S_IRUGO, show_ascii,  NULL, PSU_MFR_MODEL);
static SENSOR_DEVICE_ATTR(psu_mfr_revision, S_IRUGO, show_ascii, NULL, PSU_MFR_REVISION);
static SENSOR_DEVICE_ATTR(psu_mfr_date, S_IRUGO, show_ascii, NULL, PSU_MFR_DATE);
static SENSOR_DEVICE_ATTR(psu_mfr_serial,   S_IRUGO, show_ascii, NULL, PSU_MFR_SERIAL);
static SENSOR_DEVICE_ATTR(psu_mfr_vin_min,  S_IRUGO, show_linear, NULL, PSU_MFR_VIN_MIN);
static SENSOR_DEVICE_ATTR(psu_mfr_vin_max,  S_IRUGO, show_linear, NULL, PSU_MFR_VIN_MAX);
static SENSOR_DEVICE_ATTR(psu_mfr_vout_min, S_IRUGO, show_vout, NULL, PSU_MFR_VOUT_MIN);
static SENSOR_DEVICE_ATTR(psu_mfr_vout_max, S_IRUGO, show_vout, NULL, PSU_MFR_VOUT_MAX);
static SENSOR_DEVICE_ATTR(psu_mfr_iin_max,  S_IRUGO, show_linear, NULL, PSU_MFR_IIN_MAX);
static SENSOR_DEVICE_ATTR(psu_mfr_iout_max, S_IRUGO, show_linear, NULL, PSU_MFR_IOUT_MAX);
static SENSOR_DEVICE_ATTR(psu_mfr_pin_max,  S_IRUGO, show_linear, NULL, PSU_MFR_PIN_MAX);
static SENSOR_DEVICE_ATTR(psu_mfr_pout_max, S_IRUGO, show_linear, NULL, PSU_MFR_POUT_MAX);
static SENSOR_DEVICE_ATTR(in1_input,        S_IRUGO, show_linear,   NULL, PSU_V_IN);
static SENSOR_DEVICE_ATTR(curr1_input,      S_IRUGO, show_linear,   NULL, PSU_I_IN);
static SENSOR_DEVICE_ATTR(in2_input,        S_IRUGO, show_vout,     NULL, PSU_V_OUT);
static SENSOR_DEVICE_ATTR(curr2_input,      S_IRUGO, show_linear,   NULL, PSU_I_OUT);
static SENSOR_DEVICE_ATTR(power1_input,     S_IRUGO, show_linear,   NULL, PSU_P_IN);
static SENSOR_DEVICE_ATTR(power2_input,     S_IRUGO, show_linear,   NULL, PSU_P_OUT);
static SENSOR_DEVICE_ATTR(temp1_input,      S_IRUGO, show_linear,   NULL, PSU_TEMP1_INPUT);
static SENSOR_DEVICE_ATTR(temp2_input,      S_IRUGO, show_linear,   NULL, PSU_TEMP2_INPUT);
static SENSOR_DEVICE_ATTR(temp3_input,      S_IRUGO, show_linear,   NULL, PSU_TEMP3_INPUT);
static SENSOR_DEVICE_ATTR(fan1_input,      S_IRUGO, show_linear,   NULL, PSU_FAN1_SPEED);
static SENSOR_DEVICE_ATTR(psu_raw_v_in,      S_IRUGO, show_linear,   NULL, PSU_RAW_V_IN);
static SENSOR_DEVICE_ATTR(psu_raw_v_out,      S_IRUGO, show_linear,   NULL, PSU_RAW_V_OUT);
static SENSOR_DEVICE_ATTR(psu_raw_i_out,      S_IRUGO, show_linear,   NULL, PSU_RAW_I_OUT);
static SENSOR_DEVICE_ATTR(psu_raw_p_out,      S_IRUGO, show_linear,   NULL, PSU_RAW_P_OUT);
static SENSOR_DEVICE_ATTR(psu_raw_fan_speed,      S_IRUGO, show_linear,   NULL, PSU_RAW_FAN1_SPEED);
static SENSOR_DEVICE_ATTR(psu_raw_temp1_input,      S_IRUGO, show_linear,   NULL, PSU_RAW_TEMP1_INPUT);
static SENSOR_DEVICE_ATTR(psu_raw_temp2_input,      S_IRUGO, show_linear,   NULL, PSU_RAW_TEMP2_INPUT);
static SENSOR_DEVICE_ATTR(psu_raw_temp3_input,      S_IRUGO, show_linear,   NULL, PSU_RAW_TEMP3_INPUT);
static SENSOR_DEVICE_ATTR(psu_image_version,      S_IRUGO, show_linear,   NULL, PSU_IMAGE_VERSION);
static SENSOR_DEVICE_ATTR(psu_line_status,        S_IRUGO, show_byte,   NULL, PSU_LINE_STATUS);
static SENSOR_DEVICE_ATTR(psu_type,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TYPE_DC_AC);
static SENSOR_DEVICE_ATTR(in1_max,        S_IRUGO, show_predefine_threshold,   NULL, PSU_VIN_WARN_HI);
static SENSOR_DEVICE_ATTR(in1_min,        S_IRUGO, show_predefine_threshold,   NULL, PSU_VIN_WARN_LO);
static SENSOR_DEVICE_ATTR(in1_crit,        S_IRUGO, show_predefine_threshold,   NULL, PSU_VIN_CRIT_HI);
static SENSOR_DEVICE_ATTR(in1_lcrit,        S_IRUGO, show_predefine_threshold,   NULL, PSU_VIN_CRIT_LO);
static SENSOR_DEVICE_ATTR(fan1_max,        S_IRUGO, show_predefine_threshold,   NULL, PSU_FAN_WARN_HI);
static SENSOR_DEVICE_ATTR(fan1_min,        S_IRUGO, show_predefine_threshold,   NULL, PSU_FAN_WARN_LO);
static SENSOR_DEVICE_ATTR(fan1_crit,        S_IRUGO, show_predefine_threshold,   NULL, PSU_FAN_CRIT_HI);
static SENSOR_DEVICE_ATTR(fan1_lcrit,        S_IRUGO, show_predefine_threshold,   NULL, PSU_FAN_CRIT_LO);
static SENSOR_DEVICE_ATTR(curr1_crit,      S_IRUGO, show_predefine_threshold,   NULL, PSU_IIN_CRIT_HI);
static SENSOR_DEVICE_ATTR(curr2_crit,     S_IRUGO, show_predefine_threshold, NULL, PSU_IOUT_CRIT_HI);
static SENSOR_DEVICE_ATTR(power1_crit,     S_IRUGO, show_predefine_threshold, NULL, PSU_PIN_CRIT_HI);
static SENSOR_DEVICE_ATTR(power2_crit,     S_IRUGO, show_predefine_threshold, NULL, PSU_POUT_CRIT_HI);
static SENSOR_DEVICE_ATTR(temp1_max,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TEMP1_WARN_HI);
static SENSOR_DEVICE_ATTR(temp1_min,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TEMP1_WARN_LO);
static SENSOR_DEVICE_ATTR(temp1_crit,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TEMP1_CRIT_HI);
static SENSOR_DEVICE_ATTR(temp1_lcrit,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TEMP1_CRIT_LO);
static SENSOR_DEVICE_ATTR(temp2_max,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TEMP2_WARN_HI);
static SENSOR_DEVICE_ATTR(temp2_min,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TEMP2_WARN_LO);
static SENSOR_DEVICE_ATTR(temp2_crit,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TEMP2_CRIT_HI);
static SENSOR_DEVICE_ATTR(temp2_lcrit,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TEMP2_CRIT_LO);
static SENSOR_DEVICE_ATTR(temp3_max,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TEMP3_WARN_HI);
static SENSOR_DEVICE_ATTR(temp3_min,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TEMP3_WARN_LO);
static SENSOR_DEVICE_ATTR(temp3_crit,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TEMP3_CRIT_HI);
static SENSOR_DEVICE_ATTR(temp3_lcrit,        S_IRUGO, show_predefine_threshold,   NULL, PSU_TEMP3_CRIT_LO);
static SENSOR_DEVICE_ATTR(in2_crit,         S_IRUGO, show_predefine_threshold, NULL, PSU_VOUT_CRIT_HI);
static SENSOR_DEVICE_ATTR(in2_lcrit,        S_IRUGO, show_predefine_threshold, NULL, PSU_VOUT_CRIT_LO);

static struct attribute *ec_psu_attributes[] = {
    &sensor_dev_attr_psu_power_on.dev_attr.attr,
    &sensor_dev_attr_psu_temp_fault.dev_attr.attr,
    &sensor_dev_attr_psu_power_good.dev_attr.attr,
    &sensor_dev_attr_psu_volt_over.dev_attr.attr,
    &sensor_dev_attr_psu_curr_over.dev_attr.attr,
    &sensor_dev_attr_psu_status_word.dev_attr.attr,
    &sensor_dev_attr_psu_fan1_fault.dev_attr.attr,
    &sensor_dev_attr_psu_over_temp.dev_attr.attr,
    &sensor_dev_attr_psu_v_in.dev_attr.attr,
    &sensor_dev_attr_psu_i_in.dev_attr.attr,
    &sensor_dev_attr_psu_p_in.dev_attr.attr,
    &sensor_dev_attr_psu_v_out.dev_attr.attr,
    &sensor_dev_attr_psu_i_out.dev_attr.attr,
    &sensor_dev_attr_psu_p_out.dev_attr.attr,
    &sensor_dev_attr_psu_out_mode.dev_attr.attr,
    &sensor_dev_attr_psu_temp1_input.dev_attr.attr,
    &sensor_dev_attr_psu_temp2_input.dev_attr.attr,
    &sensor_dev_attr_psu_temp3_input.dev_attr.attr,
    &sensor_dev_attr_psu_fan1_speed_rpm.dev_attr.attr,
    &sensor_dev_attr_psu_fan1_duty_cycle_percentage.dev_attr.attr,
    &sensor_dev_attr_psu_fan_dir.dev_attr.attr,
    &sensor_dev_attr_psu_pmbus_revision.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_id.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_model.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_revision.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_date.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_serial.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_vin_min.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_vin_max.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_pout_max.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_iin_max.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_pin_max.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_vout_min.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_vout_max.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_iout_max.dev_attr.attr,
    &sensor_dev_attr_in1_input.dev_attr.attr,
    &sensor_dev_attr_curr1_input.dev_attr.attr,
    &sensor_dev_attr_in2_input.dev_attr.attr,
    &sensor_dev_attr_curr2_input.dev_attr.attr,
    &sensor_dev_attr_power1_input.dev_attr.attr,
    &sensor_dev_attr_power2_input.dev_attr.attr,
    &sensor_dev_attr_temp1_input.dev_attr.attr,
    &sensor_dev_attr_temp2_input.dev_attr.attr,
    &sensor_dev_attr_temp3_input.dev_attr.attr,
    &sensor_dev_attr_fan1_input.dev_attr.attr,
    &sensor_dev_attr_psu_raw_v_in.dev_attr.attr,
    &sensor_dev_attr_psu_raw_v_out.dev_attr.attr,
    &sensor_dev_attr_psu_raw_i_out.dev_attr.attr,
    &sensor_dev_attr_psu_raw_p_out.dev_attr.attr,
    &sensor_dev_attr_psu_raw_fan_speed.dev_attr.attr,
    &sensor_dev_attr_psu_raw_temp1_input.dev_attr.attr,
    &sensor_dev_attr_psu_raw_temp2_input.dev_attr.attr,
    &sensor_dev_attr_psu_raw_temp3_input.dev_attr.attr,
    &sensor_dev_attr_psu_image_version.dev_attr.attr,
    &sensor_dev_attr_psu_line_status.dev_attr.attr,
    &sensor_dev_attr_psu_type.dev_attr.attr,
    &sensor_dev_attr_in1_max.dev_attr.attr,
    &sensor_dev_attr_in1_min.dev_attr.attr,
    &sensor_dev_attr_in1_crit.dev_attr.attr,
    &sensor_dev_attr_in1_lcrit.dev_attr.attr,
    &sensor_dev_attr_fan1_max.dev_attr.attr,
    &sensor_dev_attr_fan1_min.dev_attr.attr,
    &sensor_dev_attr_fan1_crit.dev_attr.attr,
    &sensor_dev_attr_fan1_lcrit.dev_attr.attr,
    &sensor_dev_attr_curr1_crit.dev_attr.attr,
    &sensor_dev_attr_curr2_crit.dev_attr.attr,
    &sensor_dev_attr_power1_crit.dev_attr.attr,
    &sensor_dev_attr_power2_crit.dev_attr.attr,
    &sensor_dev_attr_temp1_max.dev_attr.attr,
    &sensor_dev_attr_temp1_min.dev_attr.attr,
    &sensor_dev_attr_temp1_crit.dev_attr.attr,
    &sensor_dev_attr_temp1_lcrit.dev_attr.attr,
    &sensor_dev_attr_temp2_max.dev_attr.attr,
    &sensor_dev_attr_temp2_min.dev_attr.attr,
    &sensor_dev_attr_temp2_crit.dev_attr.attr,
    &sensor_dev_attr_temp2_lcrit.dev_attr.attr,
    &sensor_dev_attr_temp3_max.dev_attr.attr,
    &sensor_dev_attr_temp3_min.dev_attr.attr,
    &sensor_dev_attr_temp3_crit.dev_attr.attr,
    &sensor_dev_attr_temp3_lcrit.dev_attr.attr,
    &sensor_dev_attr_in2_crit.dev_attr.attr,
    &sensor_dev_attr_in2_lcrit.dev_attr.attr,
    NULL
};

static ssize_t show_byte(struct device *dev, struct device_attribute *da,
             char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct ec_psu_data *c_data = dev_get_drvdata(dev);
    struct i2c_client *client = c_data->client;
    struct ec_psu_data *data = ec_psu_update_device(&client->dev);

    if (!data->valid) {
        return 0;
    }

    switch (attr->index) {
    case PSU_PMBUS_REVISION:
        return sprintf(buf, "%d\n", data->pmbus_revision);
    case PSU_LINE_STATUS:
        return sprintf(buf, "0x%x\n", data->line_status & 0x7);
    default:
        return sprintf(buf, "0\n");
    }
}

static ssize_t show_word(struct device *dev, struct device_attribute *da,
             char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct ec_psu_data *c_data = dev_get_drvdata(dev);
    struct i2c_client *client = c_data->client;
    struct ec_psu_data *data = ec_psu_update_device(&client->dev);
    u16 status = 0;

    if (!data->valid) {
        return 0;
    }

    switch (attr->index) {
    case PSU_POWER_ON: /* psu_power_on, low byte bit 6 of status_word, 0=>ON, 1=>OFF */
        status = (data->status_word & 0x40) ? 0 : 1;
        break;
    case PSU_TEMP_FAULT: /* psu_temp_fault, low byte bit 2 of status_word, 0=>Normal, 1=>temp fault */
        status = (data->status_word & 0x4) >> 2;
        break;
    case PSU_POWER_GOOD: /* psu_power_good, high byte bit 3 of status_word, 0=>OK, 1=>FAIL */
        status = (data->status_word & 0x800) ? 0 : 1;
        break;
    case PSU_VOLT_OVER:
        status = (data->status_word & 0x20);
        break;
    case PSU_CURR_OVER:
        status = (data->status_word & 0x10);
        break;
    case PSU_STATUS_WORD:
        status = data->status_word;
        return sprintf(buf, "0x%04x\n", status);
    default:
        return 0;
    }

    return sprintf(buf, "%ld\n", status);
}

static int two_complement_to_int(u16 data, u8 valid_bit, int mask)
{
    u16  valid_data  = data & mask;
    bool is_negative = valid_data >> (valid_bit - 1);

    return is_negative ? (-(((~valid_data) & mask) + 1)) : valid_data;
}

static ssize_t set_fan_duty_cycle(struct device *dev, struct device_attribute *da,
            const char *buf, size_t count)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct i2c_client *client = to_i2c_client(dev);
    struct ec_psu_data *data = i2c_get_clientdata(client);
    int nr = (attr->index == PSU_FAN1_DUTY_CYCLE) ? 0 : 1;
    long speed;
    int error;

    error = kstrtol(buf, 10, &speed);
    if (error)
        return error;

    if (speed < 0 || speed > MAX_FAN_DUTY_CYCLE)
        return -EINVAL;

    mutex_lock(&data->update_lock);
    data->fan_duty_cycle[nr] = speed;
    ec_psu_write_word(client, 0x3B + nr, data->fan_duty_cycle[nr]);
    mutex_unlock(&data->update_lock);

    return count;
}

static long pmbus_parse_literal_format(u16 value, int addr_index)
{
    long PMBUS_LITERAL_DATA_MULTIPLIER = 1000;
    s16 exponent = 0;
    s32 mantissa = 0;

    switch (addr_index) {
    case PSU_P_IN:
    case PSU_P_OUT:
    case PSU_MFR_PIN_MAX:
    case PSU_MFR_POUT_MAX:
        PMBUS_LITERAL_DATA_MULTIPLIER *= 1000;
        break;
    case PSU_FAN1_SPEED:
    case PSU_FAN1_DUTY_CYCLE:
        PMBUS_LITERAL_DATA_MULTIPLIER = 1;
        break;
    default:
        PMBUS_LITERAL_DATA_MULTIPLIER *= 1;
    }

    exponent = two_complement_to_int(value >> 11, 5, 0x1f);
    mantissa = two_complement_to_int(value & 0x7ff, 11, 0x7ff);

    if (exponent >= 0)
        return (mantissa << exponent) * PMBUS_LITERAL_DATA_MULTIPLIER;
    else
        return (mantissa * PMBUS_LITERAL_DATA_MULTIPLIER) / (1 << -exponent);

}

static long pmbus_parse_literal_format16(u16 value, u8 mod_val)
{
    int PMBUS_LITERAL_DATA_MULTIPLIER = 1000;
    s16 exponent = 0;
    s32 mantissa = 0;

    exponent = two_complement_to_int((mod_val << 3) >> 3, 5, 0x1f);
    mantissa = two_complement_to_int(value & 0xffff, 16, 0xffff);

    if (exponent >= 0)
        return (mantissa << exponent) * PMBUS_LITERAL_DATA_MULTIPLIER;
    else
        return (mantissa * PMBUS_LITERAL_DATA_MULTIPLIER) >> -exponent;

}

static ssize_t show_linear(struct device *dev, struct device_attribute *da,
             char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct ec_psu_data *c_data = dev_get_drvdata(dev);
    struct i2c_client *client = c_data->client;
    struct ec_psu_data *data = ec_psu_update_device(&client->dev);

    u16 value = 0;

    if (!data->valid) {
        return 0;
    }
    /*Default: multiplier=1000 => linear11(value) x multiplier*/
    switch (attr->index) {
    case PSU_V_IN:
        if (data->power_good)
            value = data->v_in;
        break;
    case PSU_I_IN:
        if (data->power_good)
            value = data->i_in;
        break;
    case PSU_P_IN:
        if (data->power_good)
            value = data->p_in;
        break;
    case PSU_V_OUT:
        if (data->power_good)
            value = data->v_out;
        break;
    case PSU_I_OUT:
        if (data->power_good)
            value = data->i_out;
        break;
    case PSU_P_OUT:
        if (data->power_good)
            value = data->p_out;
        break;
    case PSU_VOUT_MODE:
        value = data->vout_mode;
        return (sprintf(buf, "0x%02x\n", value));
    case PSU_TEMP1_INPUT:
        if (data->power_good)
            value = data->temp1;
        break;
    case PSU_TEMP2_INPUT:
        if (data->power_good)
            value = data->temp2;
        break;
    case PSU_TEMP3_INPUT:
        if (data->power_good)
            value = data->temp3;
        break;
    case PSU_FAN1_SPEED:
        if (data->power_good)
            value = data->fan_speed; /*multiplier = 1*/
        break;
    case PSU_FAN1_DUTY_CYCLE:
        if (data->power_good)
            value = data->fan_duty_cycle[0]; /*multiplier = 1*/
        break;
    case PSU_MFR_VIN_MIN:
        value = data->mfr_vin_min;
        break;
    case PSU_MFR_VIN_MAX:
        value = data->mfr_vin_max;
        break;
    case PSU_MFR_VOUT_MIN:
        value = data->mfr_vout_min;
        break;
    case PSU_MFR_VOUT_MAX:
        value = data->mfr_vout_max;
        break;
    case PSU_MFR_PIN_MAX:
        value = data->mfr_pin_max;
        break;
    case PSU_MFR_POUT_MAX:
        value = data->mfr_pout_max;
        break;
    case PSU_MFR_IOUT_MAX:
        value = data->mfr_iout_max;
        break;
    case PSU_MFR_IIN_MAX:
        value = data->mfr_iin_max;
        break;
    case PSU_RAW_V_IN:
        value = data->v_in;
        return (sprintf(buf, "0x%x\n", value));
    case PSU_RAW_V_OUT:
        value = data->v_out;
        return (sprintf(buf, "0x%x\n", value));
    case PSU_RAW_I_OUT:
        value = data->i_out;
        return (sprintf(buf, "0x%x\n", value));
    case PSU_RAW_P_OUT:
        value = data->p_out;
        return (sprintf(buf, "0x%x\n", value));
    case PSU_RAW_FAN1_SPEED:
        value = data->fan_speed;
        return (sprintf(buf, "0x%x\n", value));
    case PSU_RAW_TEMP1_INPUT:
        value = data->temp1;
        return (sprintf(buf, "0x%x\n", value));
    case PSU_RAW_TEMP2_INPUT:
        value = data->temp2;
        return (sprintf(buf, "0x%x\n", value));
    case PSU_RAW_TEMP3_INPUT:
        value = data->temp3;
        return (sprintf(buf, "0x%x\n", value));
    case PSU_IMAGE_VERSION:
        value = data->psu_image_version;
        return (sprintf(buf, "0x%04x\n", value));
    default:
        return 0;
    }

    return (sprintf(buf, "%ld\n", pmbus_parse_literal_format(value, attr->index)));
}

static ssize_t show_fan_fault(struct device *dev, struct device_attribute *da,
             char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct ec_psu_data *c_data = dev_get_drvdata(dev);
    struct i2c_client *client = c_data->client;
    struct ec_psu_data *data = ec_psu_update_device(&client->dev);
    u8 shift;

    if (!data->valid) {
        return 0;
    }

    shift = (attr->index == PSU_FAN1_FAULT) ? 7 : 6;

    return sprintf(buf, "%d\n", data->fan_fault >> shift);
}

static ssize_t show_over_temp(struct device *dev, struct device_attribute *da,
             char *buf)
{
    struct ec_psu_data *c_data = dev_get_drvdata(dev);
    struct i2c_client *client = c_data->client;
    struct ec_psu_data *data = ec_psu_update_device(&client->dev);

    if (!data->valid) {
        return 0;
    }

    return sprintf(buf, "%d\n", data->over_temp >> 7);
}

static ssize_t show_ascii(struct device *dev, struct device_attribute *da,
             char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct ec_psu_data *c_data = dev_get_drvdata(dev);
    struct i2c_client *client = c_data->client;
    struct ec_psu_data *data = ec_psu_update_device(&client->dev);
    u8 *ptr = NULL;

    if (!data->valid) {
        return 0;
    }

    switch (attr->index) {
    case PSU_FAN_DIRECTION: /* psu_fan_dir */
        ptr = data->fan_dir + 1;  /* Skip the first byte since it is the length of string. */
        /* Check if 4th bit is '1' and 3rd bit is '0' for "F2B (AFO)" FAN direction */
        if((((data->fan_dir[0] >> 3) & 1) == 0) && (((data->fan_dir[0] >> 4) & 1) == 1)) {
            strcpy(ptr,"F2B");
        }/* Check if 4th bit is '0' and 3rd bit is '1' for "B2F (AFI)" FAN direction */
        else if ((((data->fan_dir[0] >> 3) & 1) == 1) && (((data->fan_dir[0] >> 4) & 1) == 0)) {
            strcpy(ptr,"B2F");
        }
        break;
    case PSU_MFR_ID: /* psu_mfr_id */
            ptr = data->mfr_id + 1; /* The first byte is the count byte of string. */;
        break;
    case PSU_MFR_MODEL: /* psu_mfr_model */
            ptr = data->mfr_model + 1; /* The first byte is the count byte of string. */
        break;
    case PSU_MFR_REVISION: /* psu_mfr_revision */
            ptr = data->mfr_revsion + 1; /* The first byte is the count byte of string. */
        break;
    case PSU_MFR_DATE: /* psu_mfr_date */
            ptr = data->mfr_date + 1; /* The first byte is the count byte of string. */
        break;
    case PSU_MFR_SERIAL: /* psu_mfr_serial */
        ptr = data->mfr_serial + 1; /* The first byte is the count byte of string. */
        break;
    default:
        return 0;
    }

    return sprintf(buf, "%s\n", ptr);
}

static ssize_t show_vout_by_mode(struct device *dev, struct device_attribute *da,
             char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct ec_psu_data *c_data = dev_get_drvdata(dev);
    struct i2c_client *client = c_data->client;
    struct ec_psu_data *data = ec_psu_update_device(&client->dev);
    int value = 0;

    if (!data->valid) {
        return 0;
    }

    switch (attr->index) {
    case PSU_MFR_VOUT_MIN:
        value = data->mfr_vout_min;
        break;
    case PSU_MFR_VOUT_MAX:
        value = data->mfr_vout_max;
        break;
    case PSU_V_OUT:
        value = data->v_out;
        break;
    default:
        return 0;
    }

    return (sprintf(buf, "%ld\n", pmbus_parse_literal_format16(value, data->vout_mode)));
}

static ssize_t show_vout(struct device *dev, struct device_attribute *da,
             char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct ec_psu_data *data = i2c_get_clientdata(client);
    u8 *ptr = NULL;

    ptr = data->mfr_model + 1; /* The first byte is the count byte of string. */
    if ( (strncmp(ptr, "FSH082", strlen("FSH082")) == 0) || 
         (strncmp(ptr, "FSJ033", strlen("FSJ033")) == 0) || 
         (strncmp(ptr, "CDR-6011", strlen("CDR-6011")) == 0) || 
         (strncmp(ptr, "DPS-1600AB", strlen("DPS-1600AB")) == 0) || 
         (strncmp(ptr, "PTT1600", strlen("PTT1600")) == 0)  ||
         (strncmp(ptr, "PS-2302-6L", strlen("PS-2302-6L")) == 0) ||
         (strncmp(ptr, "FSJ001-612G", strlen("FSJ001-612G")) == 0) ||
         (strncmp(ptr, "FSJ001", strlen("FSJ001")) == 0) ||
         (strncmp(ptr, "FSJ004-612G", strlen("FSJ004-612G")) == 0) ||
         (strncmp(ptr, "FSJ036-610G", strlen("FSJ036-610G")) == 0) ||
         (strncmp(ptr, "FSJ035-610G", strlen("FSJ035-610G")) == 0) ||
         (strncmp(ptr, "PS-2601-6R", strlen("PS-2601-6R")) == 0) ) {
        return show_vout_by_mode(dev, da, buf);
    }
    else {
        return show_linear(dev, da, buf);
    }
}

static ssize_t show_predefine_threshold(struct device *dev, struct device_attribute *da,
             char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct ec_psu_data *data = dev_get_drvdata(dev);

    switch (attr->index){
    case PSU_VIN_WARN_HI:
        return sprintf(buf, "%d\n", data->v_in_hi * 1000);
    case PSU_VIN_WARN_LO:
        return sprintf(buf, "%d\n", data->v_in_lo * 1000);
    case PSU_VIN_CRIT_HI:
        return sprintf(buf, "%d\n", data->v_in_hi_crit * 1000);
    case PSU_VIN_CRIT_LO:
        return sprintf(buf, "%d\n", data->v_in_lo_crit * 1000);
    case PSU_VOUT_CRIT_HI:
        return sprintf(buf, "%d\n", data->v_out_hi_crit);
    case PSU_VOUT_CRIT_LO:
        return sprintf(buf, "%d\n", data->v_out_lo_crit);
    case PSU_FAN_WARN_HI:
        return sprintf(buf, "%d\n", data->fan_hi);
    case PSU_FAN_WARN_LO:
        return sprintf(buf, "%d\n", data->fan_lo);
    case PSU_FAN_CRIT_HI:
        return sprintf(buf, "%d\n", data->fan_crit);
    case PSU_FAN_CRIT_LO:
        return sprintf(buf, "%d\n", data->fan_lcrit);
    case PSU_TYPE_DC_AC:
        return sprintf(buf, "%d\n", data->psu_type);
    case PSU_IIN_CRIT_HI:
        return sprintf(buf, "%d\n", data->i_in_hi_crit * 1000);
    case PSU_IOUT_CRIT_HI:
        return sprintf(buf, "%d\n", data->i_out_hi_crit * 1000);
    case PSU_PIN_CRIT_HI:
        return sprintf(buf, "%u\n", data->p_in_hi_crit * 1000000);
    case PSU_POUT_CRIT_HI:
        return sprintf(buf, "%u\n", data->p_out_hi_crit * 1000000);
    case PSU_TEMP1_WARN_HI:
        return sprintf(buf, "%d\n", data->temp1_max * 1000);
    case PSU_TEMP1_WARN_LO:
        return sprintf(buf, "%d\n", data->temp1_min * 1000);
    case PSU_TEMP1_CRIT_HI:
        return sprintf(buf, "%d\n", data->temp1_crit * 1000);
    case PSU_TEMP1_CRIT_LO:
        return sprintf(buf, "%d\n", data->temp1_lcrit * 1000);
    case PSU_TEMP2_WARN_HI:
        return sprintf(buf, "%d\n", data->temp2_max * 1000);
    case PSU_TEMP2_WARN_LO:
        return sprintf(buf, "%d\n", data->temp2_min * 1000);
    case PSU_TEMP2_CRIT_HI:
        return sprintf(buf, "%d\n", data->temp2_crit * 1000);
    case PSU_TEMP2_CRIT_LO:
        return sprintf(buf, "%d\n", data->temp2_lcrit * 1000);
    case PSU_TEMP3_WARN_HI:
        return sprintf(buf, "%d\n", data->temp3_max * 1000);
    case PSU_TEMP3_WARN_LO:
        return sprintf(buf, "%d\n", data->temp3_min * 1000);
    case PSU_TEMP3_CRIT_HI:
        return sprintf(buf, "%d\n", data->temp3_crit * 1000);
    case PSU_TEMP3_CRIT_LO:
        return sprintf(buf, "%d\n", data->temp3_lcrit * 1000);
    default:
        return sprintf(buf, "%d\n", -1);
    }
}

static const struct attribute_group ec_psu_group = {
    .attrs = ec_psu_attributes,
};
__ATTRIBUTE_GROUPS(ec_psu);

static const char *input_status[] = {
    "Low Line",
    "No Input",
    "High Line",
    "DC Input",
    "AC Freq. > 53Hz",
    "not implemented",
    "High Line & AC Freq. > 53Hz",
    "not implemented"
};

static int psu_assign_threshold(int param_type, struct ec_psu_data *data, u16 hi, u16 lo, u16 crit_hi, u16 crit_lo)
{
    switch( param_type ){
    case THRESHOLD_PARAM_VIN:
        data->v_in_hi = hi;
        data->v_in_lo = lo;
        data->v_in_hi_crit = crit_hi;
        data->v_in_lo_crit = crit_lo;
        break;
    case THRESHOLD_PARAM_FAN:
        data->fan_hi = hi;
        data->fan_lo = lo;
        data->fan_crit = crit_hi;
        data->fan_lcrit = crit_lo;
        break;
    case THRESHOLD_PARAM_TEMP1:
        data->temp1_max = hi;
        data->temp1_min = lo;
        data->temp1_crit = crit_hi;
        data->temp1_lcrit = crit_lo;
        break;
    case THRESHOLD_PARAM_TEMP2:
        data->temp2_max = hi;
        data->temp2_min = lo;
        data->temp2_crit = crit_hi;
        data->temp2_lcrit = crit_lo;
        break;
    case THRESHOLD_PARAM_TEMP3:
        data->temp3_max = hi;
        data->temp3_min = lo;
        data->temp3_crit = crit_hi;
        data->temp3_lcrit = crit_lo;
        break;
    default:
        break;
    }
    return 0;
}

static void assign_lins_status_related_parm(struct ec_psu_data *data)
{
    switch( data->line_status & 3 ){
    case 0: /* low line */
    case 1: /* no input */
        psu_assign_threshold(THRESHOLD_PARAM_VIN, data, 133, 89, 137, 85);
        break;
    case 2: /* high line */
        psu_assign_threshold(THRESHOLD_PARAM_VIN, data, 265, 179, 269, 175);
        break;
    case 3:
        psu_assign_threshold(THRESHOLD_PARAM_VIN, data, 311, 189, 315, 185);
        break;
    default:
        break;
    }
}

static int psu_detect_threshold(struct i2c_client *client,
        struct ec_psu_data *data)
{
    u8  line_status, dcPsu = 0;

    mutex_lock(&data->update_lock);
    if( support_i2c_block == 1 ){
        /* Read mfr_model */
        u8 command = 0x9a, buf = 0;
        int length = 1;

        /* Read first byte to determine the length of data */
        ec_psu_read_block(client, command, &buf, length);
        if( buf > 0 ){
            ec_psu_read_block(client, command, data->mfr_model, buf+1);
            data->mfr_model[buf+1] = '\0';
            if( !strncmp(&data->mfr_model[1], "FSJ035", buf) ){   /*PSU = DC type*/
                dcPsu = 1;
            }
        }
    }

    line_status = i2c_smbus_read_byte_data(client, 0xD8);
    mutex_unlock(&data->update_lock);

    data->p_in_hi_crit = 1600;
    data->p_out_hi_crit = 1400;
    data->i_out_hi_crit = 133;
    data->v_out_hi_crit = 12599;
    data->v_out_lo_crit = 11398;

    if( dcPsu ){
        data->psu_type = 0;
        psu_assign_threshold(THRESHOLD_PARAM_VIN, data, 76, 39, 79, 36);
        psu_assign_threshold(THRESHOLD_PARAM_FAN, data, 0, 0, 0, 0);
        data->i_in_hi_crit = 40;

        /* warning_hi: 70, warning_lo: 0, crit_hi: 75, crit_lo: 0 */
        psu_assign_threshold(THRESHOLD_PARAM_TEMP1, data, 70, 0, 75, 0);

        /* warning_hi: 117, warning_lo: 0, crit_hi: 120, crit_lo: 0 */
        psu_assign_threshold(THRESHOLD_PARAM_TEMP2, data, 117, 0, 120, 0);

        /* warning_hi: 100, warning_lo: 0, crit_hi: 110, crit_lo: 0 */
        psu_assign_threshold(THRESHOLD_PARAM_TEMP3, data, 100, 0, 110, 0);

        dev_info(&client->dev, "DC PSU detected, model:%s\n", &data->mfr_model[1]);
    }
    else{
        data->psu_type = 1;
        data->line_status = line_status & 0x3;
        assign_lins_status_related_parm(data);
        psu_assign_threshold(THRESHOLD_PARAM_FAN, data, 23000, 6000, 24000, 5000);

        /* warning_hi: 77, warning_lo: 0, crit_hi: 80, crit_lo: 0 */
        psu_assign_threshold(THRESHOLD_PARAM_TEMP1, data, 77, 0, 80, 0);

        /* warning_hi: 110, warning_lo: 0, crit_hi: 113, crit_lo: 0 */
        psu_assign_threshold(THRESHOLD_PARAM_TEMP2, data, 110, 0, 113, 0);

        /* warning_hi: 114, warning_lo: 0, crit_hi: 117, crit_lo: 0 */
        psu_assign_threshold(THRESHOLD_PARAM_TEMP3, data, 114, 0, 117, 0);

        data->i_in_hi_crit = 15;
        data->line_status_old = data->line_status;
        dev_info(&client->dev, "line status:%d('%s')\n",
            data->line_status, input_status[data->line_status]);
    }

    return 0;
}

static int ec_psu_probe(struct i2c_client *client,
            const struct i2c_device_id *dev_id)
{
    struct ec_psu_data *data;
    struct device     *hwmon_dev;

    if (!i2c_check_functionality(client->adapter,
        I2C_FUNC_SMBUS_BYTE_DATA |
        I2C_FUNC_SMBUS_WORD_DATA )) {
        return -EIO;
    }

    if (!i2c_check_functionality(client->adapter,
        I2C_FUNC_SMBUS_I2C_BLOCK)) {
        support_i2c_block = 0;
    }

    data = devm_kzalloc(&client->dev, sizeof(struct ec_psu_data), GFP_KERNEL);
    if (!data) {
        return -ENOMEM;
    }

    i2c_set_clientdata(client, data);
    /*record client info*/
    data->client = client;
    mutex_init(&data->update_lock);
    data->chip = dev_id->driver_data;
    dev_info(&client->dev, "chip found\n");

    psu_detect_threshold(client, data);

    /* Everything is ready, now register the working device */
    hwmon_dev = devm_hwmon_device_register_with_groups(&client->dev, "ec_psu", data, ec_psu_groups);
    if (IS_ERR(hwmon_dev)) {
        return PTR_ERR(hwmon_dev);
    }

    dev_info(&client->dev, "%s: psu '%s'\n", dev_name(hwmon_dev), client->name);

    return 0;
}

static const struct i2c_device_id ec_psu_id[] = {
    { "ec-common-psu", 0 },
    {}
};
MODULE_DEVICE_TABLE(i2c, ec_psu_id);

static struct i2c_driver ec_psu_driver = {
    .class      = I2C_CLASS_HWMON,
    .driver = {
        .name   = "ec-common-psu",
    },
    .probe    = ec_psu_probe,
    .id_table = ec_psu_id,
    .address_list = normal_i2c,
};

static int ec_psu_read_byte(struct i2c_client *client, u8 reg)
{
    int status = 0, retry = I2C_RW_RETRY_COUNT;

    while (retry) {
        status = i2c_smbus_read_byte_data(client, reg);
        if (unlikely(status < 0)) {
            msleep(I2C_RW_RETRY_INTERVAL);
            retry--;
            continue;
        }

        break;
    }

    return status;
}

static int ec_psu_read_word(struct i2c_client *client, u8 reg)
{
    int status = 0, retry = I2C_RW_RETRY_COUNT;

    while (retry) {
        status = i2c_smbus_read_word_data(client, reg);
        if (unlikely(status < 0)) {
            msleep(I2C_RW_RETRY_INTERVAL);
            retry--;
            continue;
        }

        break;
    }

    return status;
}

static int ec_psu_write_word(struct i2c_client *client, u8 reg, u16 value)
{
    int status = 0, retry = I2C_RW_RETRY_COUNT;

    while (retry) {
        status = i2c_smbus_write_word_data(client, reg, value);
        if (unlikely(status < 0)) {
            msleep(I2C_RW_RETRY_INTERVAL);
            retry--;
            continue;
        }

        break;
    }

    return status;
}

static int ec_psu_read_block(struct i2c_client *client, u8 command, u8 *data,
              int data_len)
{
    int status = 0, retry = I2C_RW_RETRY_COUNT;

    while (retry) {
        status = i2c_smbus_read_i2c_block_data(client, command, data_len, data);
        if (unlikely(status < 0)) {
            msleep(I2C_RW_RETRY_INTERVAL);
            retry--;
            continue;
        }

        break;
    }

    return status;
}

struct reg_data_byte {
    u8   reg;
    u8  *value;
};

struct reg_data_word {
    u8   reg;
    u16 *value;
};

static struct ec_psu_data *ec_psu_update_device(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct ec_psu_data *data = i2c_get_clientdata(client);

    mutex_lock(&data->update_lock);

    if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
        || !data->valid) {
        int i, status, length;
        u8 command, buf;
        struct reg_data_byte regs_byte[] = { {0x19, &data->capability},
                                             {0x20, &data->vout_mode},
                                             {0x7d, &data->over_temp},
                                             {0x81, &data->fan_fault},
                                             {0x98, &data->pmbus_revision},
                                             {0xd8, &data->line_status}};
        struct reg_data_word regs_word[] = { {0x79, &data->status_word},
                                             {0x88, &data->v_in},
                                             {0x8b, &data->v_out},
                                             {0x89, &data->i_in},
                                             {0x8c, &data->i_out},
                                             {0x97, &data->p_in},
                                             {0x96, &data->p_out},
                                             {0x8d, &data->temp1},
                                             {0x8e, &data->temp2},
                                             {0x8f, &data->temp3},
                                             {0x3b, &(data->fan_duty_cycle[0])},
                                             {0x3c, &(data->fan_duty_cycle[1])},
                                             {0x90, &data->fan_speed},
                                             {0xa0, &data->mfr_vin_min},
                                             {0xa1, &data->mfr_vin_max},
                                             {0xa2, &data->mfr_iin_max},
                                             {0xa3, &data->mfr_pin_max},
                                             {0xa4, &data->mfr_vout_min},
                                             {0xa5, &data->mfr_vout_max},
                                             {0xa6, &data->mfr_iout_max},
                                             {0xa7, &data->mfr_pout_max},
                                             {0xd7, &data->psu_image_version}};

        dev_dbg(&client->dev, "Starting ym2651 update\n");
        data->valid = 0;
        data->power_good = 0;

        /* Read byte data */
        for (i = 0; i < ARRAY_SIZE(regs_byte); i++) {
            status = ec_psu_read_byte(client, regs_byte[i].reg);

            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n",
                        regs_byte[i].reg, status);
                goto exit;
            }
            else {
                *(regs_byte[i].value) = status;
            }
        }

        /* Read word data */
        for (i = 0; i < ARRAY_SIZE(regs_word); i++) {
            status = ec_psu_read_word(client, regs_word[i].reg);

            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n",
                        regs_word[i].reg, status);
                goto exit;
            }
            else {
                *(regs_word[i].value) = status;
            }
        }

        if (support_i2c_block) {

            /* Read fan_direction */
            command = 0xC3;
            status = ec_psu_read_block(client, command, data->fan_dir,
                                         ARRAY_SIZE(data->fan_dir)-1);
            data->fan_dir[ARRAY_SIZE(data->fan_dir)-1] = '\0';

            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n", command, status);
                goto exit;
            }

            /* Read mfr_id */
            command = 0x99;
            length  = 1;

            /* Read first byte to determine the length of data */
            status = ec_psu_read_block(client, command, &buf, length);
            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n", command, status);
                goto exit;
            }

            if( buf > (ARRAY_SIZE(data->mfr_id) -2 ) ){
                printk("psu drv:id buffer size too small(%d, %d)\n", buf, ARRAY_SIZE(data->mfr_id) -2);
                buf = ARRAY_SIZE(data->mfr_id) - 2;
            }
            status = ec_psu_read_block(client, command, data->mfr_id, buf+1);
            data->mfr_id[buf+1] = '\0';

            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n", command, status);
                goto exit;
            }

            /* Read mfr_model */
            command = 0x9a;

            /* Read first byte to determine the length of data */
            status = ec_psu_read_block(client, command, &buf, length);
            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n", command, status);
                goto exit;
            }

            if( buf > (ARRAY_SIZE(data->mfr_model) -2 ) ){
                printk("psu drv:model buffer size too small(%d, %d)\n", buf, ARRAY_SIZE(data->mfr_model) -2);
                buf = ARRAY_SIZE(data->mfr_model) - 2;
            }
            status = ec_psu_read_block(client, command, data->mfr_model, buf+1);
            data->mfr_model[buf+1] = '\0';

            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n", command, status);
                goto exit;
            }

            /* Read mfr_revsion */
            command = 0x9b;

            /* Read first byte to determine the length of data */
            status = ec_psu_read_block(client, command, &buf, length);
            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n", command, status);
                goto exit;
            }

            if( buf > (ARRAY_SIZE(data->mfr_revsion) -2 ) ){
                printk("psu drv:revision buffer size too small(%d, %d)\n", buf, ARRAY_SIZE(data->mfr_revsion) -2);
                buf = ARRAY_SIZE(data->mfr_revsion) - 2;
            }
            status = ec_psu_read_block(client, command, data->mfr_revsion, buf+1);
            data->mfr_revsion[buf+1] = '\0';

            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n", command, status);
                goto exit;
            }

            /* Read mfr_date */
            command = 0x9d;
            /* Read first byte to determine the length of data */
            status = ec_psu_read_block(client, command, &buf, length);
            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n", command, status);
                goto exit;
            }
            if( buf > (ARRAY_SIZE(data->mfr_date) -2 ) ){
                printk("psu drv:date buffer size too small(%d, %d)\n", buf, ARRAY_SIZE(data->mfr_date) -2);
                buf = ARRAY_SIZE(data->mfr_date) - 2;
            }
            status = ec_psu_read_block(client, command, data->mfr_date, buf+1);
            data->mfr_date[buf+1] = '\0';

            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n", command, status);
                goto exit;
            }

            /* Read mfr_serial */
            command = 0x9e;
            /* Read first byte to determine the length of data */
            status = ec_psu_read_block(client, command, &buf, length);
            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n", command, status);
                goto exit;
            }

            if( buf > (ARRAY_SIZE(data->mfr_serial) -2 ) ){
                printk("psu drv:serial buffer size too small(%d, %d)\n", buf, ARRAY_SIZE(data->mfr_serial) -2);
                buf = ARRAY_SIZE(data->mfr_serial) - 2;
            }
            status = ec_psu_read_block(client, command, data->mfr_serial, buf+1);
            data->mfr_serial[buf+1] = '\0';

            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n", command, status);
                goto exit;
            }
        }
        if( data->line_status_old != (data->line_status & 3) ){
            assign_lins_status_related_parm(data);
            data->line_status_old = data->line_status & 3;
        }
        data->power_good = (data->status_word & 0x800) ? 0 : 1;

        data->last_updated = jiffies;
        data->valid = 1;
    }

exit:
    mutex_unlock(&data->update_lock);

    return data;
}

static int __init ec_psu_init(void)
{
    return i2c_add_driver(&ec_psu_driver);
}

static void __exit ec_psu_exit(void)
{
    i2c_del_driver(&ec_psu_driver);
}

MODULE_AUTHOR("Michael Shih <michael_shih@edge-core.com>");
MODULE_DESCRIPTION("PMBus driver for EDGE-CORE Network");
MODULE_LICENSE("GPL");

module_init(ec_psu_init);
module_exit(ec_psu_exit);
