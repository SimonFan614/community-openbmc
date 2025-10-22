/*
 * ec_driver.c - The i2c driver for Accton COMe EC.
 *
 * Copyright 2019-present Accton. All Rights Reserved.
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

// #define DEBUG

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#ifdef DEBUG
#define EC_DEBUG(fmt, ...) do {                   \
  printk(KERN_DEBUG "%s:%d " fmt "\n",            \
         __FUNCTION__, __LINE__, ##__VA_ARGS__);  \
} while (0)

#else /* !DEBUG */

#define EC_DEBUG(fmt, ...)
#endif

struct ec_data {
  struct i2c_client *client;
  struct mutex update_lock;
  u8 reg_offset;
};

enum attr_index {
  POWER_STATE,
  POWER_PHASE,
  CPU_STATUS,
  CPU_TEMP,
  DIMM0_TEMP,
  DIMM1_TEMP,

  CPU_CORE_VOLT,
  CPU_3V_VOLT,
  CPU_5V_VOLT,
  CPU_12V_VOLT,
  DIMM_VOLT,

  PCB_VERSION,
  BUILD_YEAR,
  BUILD_MONTH,
  BUILD_DATE,
  VER_MAJOR,
  VER_MINOR,
  POST_CODE,
  RESET_REASON,
  CPLD_VERSION,
  RESET_POWER_ON,
  RESET_EC_WATCHDOG,
  RESET_POWER_BUTTON,
  RESET_SYSTEM_BUTTON,
  RESET_CPU_WARM,
  RESET_CPU_COLD,
  RESET_CPU_WATCHDOG,
  RESET_DIMM_CRIT,
};

enum ec_regs {
  COMe_POWER_STATE          = 0x00,
  COMe_POWER_PHASE          = 0x01,
  COMe_CPU_STATUS           = 0x04,
  CPU_TEMPERATURE           = 0x10,
  DIMM0_TEMPERATURE         = 0x12,
  DIMM1_TEMPERATURE         = 0x13,

  CPU_CORE_VOLTAGE          = 0x16,
  CPU_3V_VOLTAGE            = 0x18,
  CPU_5V_VOLTAGE            = 0x1A,
  CPU_12V_VOLTAGE           = 0x1C,
  DIMM_VOLTAGE              = 0x1E,

  EC_PCB_VERSION            = 0xFA,
  EC_BUILD_YEAR             = 0xFB,
  EC_BUILD_MONTH            = 0xFC,
  EC_BUILD_DATE             = 0xFD,
  EC_VER_MAJOR              = 0xFE,
  EC_VER_MINOR              = 0xFF,
  EC_POST_CODE              = 0x05,
  EC_RESET_REASON           = 0x30,
  EC_BIOS_SPI_CS            = 0x21,
};

static const char * const label_names[] = {
	[CPU_TEMP]       = "CPU_Temp",
	[DIMM0_TEMP]     = "DIMM0_Temp",
	[DIMM1_TEMP]     = "DIMM1_Temp",
	[CPU_CORE_VOLT]  = "CPU_Core_Volt",
	[CPU_3V_VOLT]    = "CPU_3V_Volt",
	[CPU_5V_VOLT]    = "CPU_5V_Volt",
	[CPU_12V_VOLT]   = "CPU_12V_Volt",
	[DIMM_VOLT]      = "DIMM_Volt",
};

static int ec_read_word_data(struct device *dev, u8 reg) {
  struct ec_data *data = dev_get_drvdata(dev);
  int value = -1, count = 5;

  mutex_lock(&data->update_lock);

  while((value < 0 || value == 0xffff) && count--) {
    value = i2c_smbus_read_word_data(data->client, reg);
  }

  mutex_unlock(&data->update_lock);

  if (value < 0) {
    /* error case */
    EC_DEBUG("I2C read error, value: %d\n", value);
    return -1;
  }

  return value;
}

static int ec_read_byte_data(struct device *dev, u8 reg) {
  struct ec_data *data = dev_get_drvdata(dev);
  int value = -1, count = 5;

  mutex_lock(&data->update_lock);

  while ((value < 0 || value == 0xffff) && count--) {
    value = i2c_smbus_read_byte_data(data->client, reg);
  }

  mutex_unlock(&data->update_lock);

  if (value < 0) {
    /* error case */
    EC_DEBUG("I2C read error, value: %d\n", value);
    return -1;
  }

  return value;
}

static int ec_write_byte_data(struct device *dev, u8 reg, u8 value) {
  struct ec_data *data = dev_get_drvdata(dev);
  int ret = -1, count = 5;

  mutex_lock(&data->update_lock);

  while (ret && count--) {
    ret = i2c_smbus_write_byte_data(data->client, reg, value);
  }

  mutex_unlock(&data->update_lock);

  if (ret < 0) {
    /* error case */
    EC_DEBUG("I2C write error, ret: %d\n", ret);
    return -1;
  }

  return 0;
}

static int ec_voltage_convert(struct device *dev, u8 reg) {
  struct ec_data *data = dev_get_drvdata(dev);
  int value = 0;
  int value_msb = -1;
  int value_lsb = -1;
  int count = 5;

  mutex_lock(&data->update_lock);

  while ((value_msb < 0 || value_msb == 0xffff) && count--) {
    value_msb = i2c_smbus_read_byte_data(data->client, reg);
  }

  if (value_msb < 0) {
    /* error case */
    EC_DEBUG("I2C read error, value: %d\n", value_msb);
    mutex_unlock(&data->update_lock);
    return -1;
  }

  while ((value_lsb < 0 || value_lsb == 0xffff) && count--) {
    value_lsb = i2c_smbus_read_byte_data(data->client, reg+1);
  }

  if (value_lsb < 0) {
    /* error case */
    EC_DEBUG("I2C read error, value: %d\n", value_lsb);
    mutex_unlock(&data->update_lock);
    return -1;
  }

  mutex_unlock(&data->update_lock);

  /*
   * Voltage (mv) = 1000 * (Vol_MSB + (Vol_LSB * 3.9) / 1000))
   *              = 1000 * Vol_MSB + (Vol_LSB * 3.9)
   *              = ((10000 * Vol_MSB) + (Vol_LSB * 39)) / 10
   */
  value = ((10000 * value_msb) + (value_lsb * 39)) / 10;

  return value;
}

static ssize_t ec_value_show(struct device *dev,
                                  struct device_attribute *dev_attr, char *buf) {
  struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
  int value = 0, ret;

  switch(attr->index) {
    case POWER_STATE:
      value = ec_read_byte_data(dev, COMe_POWER_STATE);
      if (value < 0)
        return -1;
      return scnprintf(buf, PAGE_SIZE, "0x%02x\n", value);
    case POWER_PHASE:
      value = ec_read_byte_data(dev, COMe_POWER_PHASE);
      if (value < 0)
        return -1;
      return scnprintf(buf, PAGE_SIZE, "0x%02x\n", value);
    case CPU_STATUS:
      value = ec_read_byte_data(dev, COMe_CPU_STATUS);
      if (value < 0)
        return -1;
      return scnprintf(buf, PAGE_SIZE, "0x%02x\n", value);
    case CPU_TEMP:
      value = ec_read_byte_data(dev, CPU_TEMPERATURE);
      if (value < 0)
        return -1;
      else
        value = value * 1000;
      break;
    case DIMM0_TEMP:
      value = ec_read_byte_data(dev, DIMM0_TEMPERATURE);
      if (value < 0)
        return -1;
      else
        value = ((s8)value) * 1000;
      break;
    case DIMM1_TEMP:
      value = ec_read_byte_data(dev, DIMM1_TEMPERATURE);
      if (value < 0)
        return -1;
      else
        value = ((s8)value) * 1000;
      break;
    case CPU_CORE_VOLT:
      value = ec_voltage_convert(dev, CPU_CORE_VOLTAGE);
      if (value < 0)
        return -1;
      break;
    case CPU_3V_VOLT:
      value = ec_voltage_convert(dev, CPU_3V_VOLTAGE);
      if (value < 0)
        return -1;
      break;
    case CPU_5V_VOLT:
      value = ec_voltage_convert(dev, CPU_5V_VOLTAGE);
      if (value < 0)
        return -1;
      break;
    case CPU_12V_VOLT:
      value = ec_voltage_convert(dev, CPU_12V_VOLTAGE);
      if (value < 0)
        return -1;
      break;
    case DIMM_VOLT:
      value = ec_voltage_convert(dev, DIMM_VOLTAGE);
      if (value < 0)
        return -1;
      break;
    case VER_MAJOR:
      value = ec_read_byte_data(dev, EC_VER_MAJOR);
      if (value < 0)
        return -1;
      break;
    case VER_MINOR:
      value = ec_read_byte_data(dev, EC_VER_MINOR);
      if (value < 0)
        return -1;
      break;
    case PCB_VERSION:
      value = ec_read_byte_data(dev, EC_PCB_VERSION);
      if (value < 0)
        return -1;
      return scnprintf(buf, PAGE_SIZE, "0x%02x\n", value);
    case BUILD_YEAR:
      value = ec_read_byte_data(dev, EC_BUILD_YEAR);
      if (value < 0)
        return -1;
      break;
    case BUILD_MONTH:
      value = ec_read_byte_data(dev, EC_BUILD_MONTH);
      if (value < 0)
        return -1;
      break;
    case BUILD_DATE:
      value = ec_read_byte_data(dev, EC_BUILD_DATE);
      if (value < 0)
        return -1;
      break;
    case POST_CODE:
      value = ec_read_byte_data(dev, EC_POST_CODE);
      if (value < 0)
        return -1;
      return scnprintf(buf, PAGE_SIZE, "0x%02x\n", value);
    case RESET_REASON:
      value = ec_read_byte_data(dev, EC_RESET_REASON);
      if (value < 0)
        return -1;
      return scnprintf(buf, PAGE_SIZE, "0x%02x\n", value);
    case CPLD_VERSION:
      value = ec_read_byte_data(dev, EC_VER_MAJOR);
      if (value < 0)
        return -1;
      ret = scnprintf(buf, PAGE_SIZE, "0x%02x", value);
      value = ec_read_byte_data(dev, EC_VER_MINOR);
      if (value < 0)
        return ret;
      return ret + scnprintf(&buf[ret], PAGE_SIZE, " 0x%02x\n", value);
    case RESET_POWER_ON:
    case RESET_EC_WATCHDOG:
    case RESET_POWER_BUTTON:
    case RESET_SYSTEM_BUTTON:
    case RESET_CPU_WARM:
    case RESET_CPU_COLD:
    case RESET_CPU_WATCHDOG:
    case RESET_DIMM_CRIT:
      value = ec_read_byte_data(dev, EC_RESET_REASON);
      if (value < 0)
        return -1;

      value = ((1 << (attr->index - RESET_POWER_ON) ) & value) ? 1 : 0;
  }

  return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t ec_label_show(struct device *dev,
                                  struct device_attribute *dev_attr, char *buf) {
  struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);

  return sprintf(buf, "%s\n", label_names[attr->index]);
}

static ssize_t bios_spi_cs_store(struct device *dev,
                                  struct device_attribute *dev_attr, const char *buf, size_t count) {
  u16 set_value = 0;
  u16 chip_cs = 0;

  int ret =  kstrtou16(buf, 16, &chip_cs);
  if (ret < 0)
        return ret;

  switch( chip_cs ){
  case 0:
      set_value = 0;
      break;
  case 1:
      set_value = 0x1e;
      break;
  case 2:
      set_value = 0x1f;
      break;
  default:
      return -1;
  }

  ec_write_byte_data(dev, EC_BIOS_SPI_CS, set_value & 0xff);
  return count;
}

static ssize_t bios_spi_cs_show(struct device *dev,
                                  struct device_attribute *dev_attr, char *buf) {
  int value = 0;

  value = ec_read_byte_data(dev, EC_BIOS_SPI_CS);
  if (value < 0)
         return -1;

  return sprintf(buf, "0x%02x\n", value & 0xff);
}

static ssize_t ec_offset_store(struct device *dev,
                                  struct device_attribute *dev_attr, const char *buf, size_t count) {
  struct ec_data *data = dev_get_drvdata(dev);
  u16 set_offset = 0;

  if (data == NULL)
        return -1;

  int ret =  kstrtou16(buf, 16, &set_offset);
  if (ret < 0)
        return ret;

  if((set_offset >= 256) || (set_offset < 0)) {
        printk("register offset err!\n");
        return -2;
  }

  data->reg_offset = set_offset;
  return count;
}

static ssize_t ec_offset_show(struct device *dev,
                                  struct device_attribute *dev_attr, char *buf) {
  struct ec_data *data = dev_get_drvdata(dev);
  return sprintf(buf, "0x%02x\n", data ? data->reg_offset : -1);
}

static ssize_t reg_value_store(struct device *dev,
                                  struct device_attribute *dev_attr, const char *buf, size_t count) {
  struct ec_data *data = dev_get_drvdata(dev);
  u16 set_value = 0;

  if (data == NULL)
        return -1;

  int ret =  kstrtou16(buf, 16, &set_value);
  if (ret < 0)
        return ret;

  if((set_value >= 256) || (set_value < 0)) {
        printk("register value err!\n");
        return -2;
  }

  ec_write_byte_data(dev, data->reg_offset, set_value & 0xff);
  return count;
}

static ssize_t reg_value_show(struct device *dev,
                                  struct device_attribute *dev_attr, char *buf) {
  struct ec_data *data = dev_get_drvdata(dev);
  int value = 0;

  value = ec_read_byte_data(dev, data->reg_offset);
  if (value < 0)
         return -1;

  return sprintf(buf, "0x%02x\n", data ? (value & 0xff) : -1);
}

/* COMe & CPU status */
static SENSOR_DEVICE_ATTR_RO(power_state, ec_value, POWER_STATE);
static SENSOR_DEVICE_ATTR_RO(power_phase, ec_value, POWER_PHASE);
static SENSOR_DEVICE_ATTR_RO(cpu_status, ec_value, CPU_STATUS);

/* Voltages */
static SENSOR_DEVICE_ATTR_RO(in0_input, ec_value, CPU_CORE_VOLT);
static SENSOR_DEVICE_ATTR_RO(in1_input, ec_value, CPU_3V_VOLT);
static SENSOR_DEVICE_ATTR_RO(in2_input, ec_value, CPU_5V_VOLT);
static SENSOR_DEVICE_ATTR_RO(in3_input, ec_value, CPU_12V_VOLT);
static SENSOR_DEVICE_ATTR_RO(in4_input, ec_value, DIMM_VOLT);

static SENSOR_DEVICE_ATTR_RO(in0_label, ec_label, CPU_CORE_VOLT);
static SENSOR_DEVICE_ATTR_RO(in1_label, ec_label, CPU_3V_VOLT);
static SENSOR_DEVICE_ATTR_RO(in2_label, ec_label, CPU_5V_VOLT);
static SENSOR_DEVICE_ATTR_RO(in3_label, ec_label, CPU_12V_VOLT);
static SENSOR_DEVICE_ATTR_RO(in4_label, ec_label, DIMM_VOLT);

/* Temperature */
static SENSOR_DEVICE_ATTR_RO(temp1_input, ec_value, CPU_TEMP);
static SENSOR_DEVICE_ATTR_RO(temp2_input, ec_value, DIMM0_TEMP);
static SENSOR_DEVICE_ATTR_RO(temp3_input, ec_value, DIMM1_TEMP);

static SENSOR_DEVICE_ATTR_RO(temp1_label, ec_label, CPU_TEMP);
static SENSOR_DEVICE_ATTR_RO(temp2_label, ec_label, DIMM0_TEMP);
static SENSOR_DEVICE_ATTR_RO(temp3_label, ec_label, DIMM1_TEMP);

/* Firmware version */
static SENSOR_DEVICE_ATTR_RO(fw_ver_major, ec_value, VER_MAJOR);
static SENSOR_DEVICE_ATTR_RO(fw_ver_minor, ec_value, VER_MINOR);

/* Build data */
static SENSOR_DEVICE_ATTR_RO(build_year, ec_value, BUILD_YEAR);
static SENSOR_DEVICE_ATTR_RO(build_month, ec_value, BUILD_MONTH);
static SENSOR_DEVICE_ATTR_RO(build_date, ec_value, BUILD_DATE);

static SENSOR_DEVICE_ATTR_RO(board_ver, ec_value, PCB_VERSION);
static SENSOR_DEVICE_ATTR_RO(post_code, ec_value, POST_CODE);
static SENSOR_DEVICE_ATTR_RO(reset_reason, ec_value, RESET_REASON);
static SENSOR_DEVICE_ATTR_RO(cpld_version, ec_value, CPLD_VERSION);
static SENSOR_DEVICE_ATTR_RW(bios_spi_cs, bios_spi_cs, 0);
static SENSOR_DEVICE_ATTR_RO(reason_power_on, ec_value, RESET_POWER_ON);
static SENSOR_DEVICE_ATTR_RO(reason_ec_wtd, ec_value, RESET_EC_WATCHDOG);
static SENSOR_DEVICE_ATTR_RO(reason_power_button, ec_value, RESET_POWER_BUTTON);
static SENSOR_DEVICE_ATTR_RO(reason_system_button, ec_value, RESET_SYSTEM_BUTTON);
static SENSOR_DEVICE_ATTR_RO(reason_cpu_warm, ec_value, RESET_CPU_WARM);
static SENSOR_DEVICE_ATTR_RO(reason_cpu_cold, ec_value, RESET_CPU_COLD);
static SENSOR_DEVICE_ATTR_RO(reason_cpu_wtd, ec_value, RESET_CPU_WATCHDOG);
static SENSOR_DEVICE_ATTR_RO(reason_dimm_crit, ec_value, RESET_DIMM_CRIT);
static SENSOR_DEVICE_ATTR_RW(reg_offset, ec_offset, 0);
static SENSOR_DEVICE_ATTR_RW(reg_value, reg_value, 0);

static struct attribute *ec_driver_attrs[] =  {
    &sensor_dev_attr_power_state.dev_attr.attr,
    &sensor_dev_attr_power_phase.dev_attr.attr,
    &sensor_dev_attr_cpu_status.dev_attr.attr,
    &sensor_dev_attr_in0_input.dev_attr.attr,
    &sensor_dev_attr_in1_input.dev_attr.attr,
    &sensor_dev_attr_in2_input.dev_attr.attr,
    &sensor_dev_attr_in3_input.dev_attr.attr,
    &sensor_dev_attr_in4_input.dev_attr.attr,
    &sensor_dev_attr_in0_label.dev_attr.attr,
    &sensor_dev_attr_in1_label.dev_attr.attr,
    &sensor_dev_attr_in2_label.dev_attr.attr,
    &sensor_dev_attr_in3_label.dev_attr.attr,
    &sensor_dev_attr_in4_label.dev_attr.attr,
    &sensor_dev_attr_temp1_input.dev_attr.attr,
    &sensor_dev_attr_temp2_input.dev_attr.attr,
    &sensor_dev_attr_temp3_input.dev_attr.attr,
    &sensor_dev_attr_temp1_label.dev_attr.attr,
    &sensor_dev_attr_temp2_label.dev_attr.attr,
    &sensor_dev_attr_temp3_label.dev_attr.attr,
    &sensor_dev_attr_fw_ver_major.dev_attr.attr,
    &sensor_dev_attr_fw_ver_minor.dev_attr.attr,
    &sensor_dev_attr_build_year.dev_attr.attr,
    &sensor_dev_attr_build_month.dev_attr.attr,
    &sensor_dev_attr_build_date.dev_attr.attr,
    &sensor_dev_attr_board_ver.dev_attr.attr,
    &sensor_dev_attr_post_code.dev_attr.attr,
    &sensor_dev_attr_reset_reason.dev_attr.attr,
    &sensor_dev_attr_cpld_version.dev_attr.attr,
    &sensor_dev_attr_bios_spi_cs.dev_attr.attr,
    &sensor_dev_attr_reason_power_on.dev_attr.attr,
    &sensor_dev_attr_reason_ec_wtd.dev_attr.attr,
    &sensor_dev_attr_reason_power_button.dev_attr.attr,
    &sensor_dev_attr_reason_system_button.dev_attr.attr,
    &sensor_dev_attr_reason_cpu_warm.dev_attr.attr,
    &sensor_dev_attr_reason_cpu_cold.dev_attr.attr,
    &sensor_dev_attr_reason_cpu_wtd.dev_attr.attr,
    &sensor_dev_attr_reason_dimm_crit.dev_attr.attr,
    &sensor_dev_attr_reg_offset.dev_attr.attr,
    &sensor_dev_attr_reg_value.dev_attr.attr,
    NULL,
};
ATTRIBUTE_GROUPS(ec_driver);

/* ec_driver id */
static const struct i2c_device_id ec_driver_id[] = {
  {"ec_driver", 0},
  { },
};
MODULE_DEVICE_TABLE(i2c, ec_driver_id);


static int ec_driver_probe(struct i2c_client *client,
                         const struct i2c_device_id *id) {
  struct i2c_adapter *adapter = client->adapter;
  struct device *dev = &client->dev;
  struct ec_data *data;
  struct device *hwmon_dev;

  if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
    return -ENODEV;

  data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
  if (!data)
    return -ENOMEM;

  data->client = client;
  data->reg_offset = -1;
  mutex_init(&data->update_lock);

  hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
                                                     data, ec_driver_groups);
  return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id ec_com_e_drv_of_match[] = {
  { .compatible = "edge-core,ec_driver" },
  {}
};
MODULE_DEVICE_TABLE(of, ec_com_e_drv_of_match);

static struct i2c_driver ec_driver = {
  .class    = I2C_CLASS_HWMON,
  .driver = {
    .name = "ec_driver",
    .of_match_table = ec_com_e_drv_of_match,
  },
  .probe    = ec_driver_probe,
  .id_table = ec_driver_id,
};

module_i2c_driver(ec_driver);

MODULE_AUTHOR("Neal Chen <neal_chen@accton.com>");
MODULE_DESCRIPTION("Edge-core COMe EC Driver");
MODULE_LICENSE("GPL");
