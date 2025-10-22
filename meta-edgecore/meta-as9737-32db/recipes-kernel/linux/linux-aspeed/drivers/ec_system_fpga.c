/*
 * ec_system_fpga.c - Part of Linux kernel modules for hardware
 *             monitoring
 *
 * Copyright (C) 2019-2029 Simon Fann <simon_fan@edge-core.com>
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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/ctype.h>

#define FPGA_CMD_GET_BUFFER_LEN    64

#define FPGA_SYS_I2C_REG_BD_INFO    0x00
#define FPGA_SYS_I2C_REG_CPLD_VER_MAJ    0x01
#define FPGA_SYS_I2C_REG_PRESENT         0X03
#define FPGA_SYS_I2C_REG_PSU_STATUS      0x04
#define FPGA_SYS_I2C_REG_RESET3          0x07
#define FPGA_SYS_I2C_REG_POWER_OFF       0x08
#define FPGA_SYS_I2C_REG_MISC2           0x1d
#define FPGA_SYS_I2C_REG_E_FUSE_CTRL     0x27
    #define FPGA_FUSE_CTRL_COM_E_EN_BIT  7
#define FPGA_SYS_I2C_REG_BMC_RELATE      0x60
#define FPGA_SYS_I2C_REG_UART_SELECT     0x62
    #define FPGA_UART_SELECT_HOTKEY      (1 << 2)
    #define FPGA_UART_SELECT_CONNECT_CPU (1 << 0)

#define SYS_FPGA_REG_BIT(x)              (1 << (x))

#define SYS_FPGA_ENABLE_STR(x) \
    if( !strncmp(x, "on", 2) || !strncmp(x, "enable", 6) || \
        !strncmp(x, "1", 1) )

#define SYS_FPGA_DISABLE_STR(x) \
    if( !strncmp(x, "off", 3) || !strncmp(x, "disable", 7) || \
        !strncmp(x, "0", 1) )

struct ec_sys_fpga_data {
    struct i2c_client *client;
    struct mutex update_lock;

    char valid;                     /* !=0 if following fields are valid */
    unsigned long last_updated;     /* In jiffies */
    u8  reg_offset;
};

static int check_same_value(int onoff, s32 reg_val, int bit)
{
   if( (onoff && (reg_val & SYS_FPGA_REG_BIT(bit)) ) ||
       (!onoff && !(reg_val & SYS_FPGA_REG_BIT(bit)) ) )
        return 1;
    else
        return -1;
}

static s32 set_reg_value(int onoff, s32 reg_val, int bit)
{
    if( onoff )
        reg_val |= SYS_FPGA_REG_BIT(bit);
    else
        reg_val &= ~(SYS_FPGA_REG_BIT(bit));

    return reg_val;
}

/* read BD Information */
static ssize_t show_fpga_bd(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    s32 value = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_BD_INFO);

    return sprintf(buf, "0x%02x\n", value & 0x3);
}

/* read CPLD version */
static ssize_t show_fpga_version(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;

    s32 value_maj = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_CPLD_VER_MAJ);

    return sprintf(buf, "0x%02x\n", value_maj & 0xff);
}

static ssize_t show_power_supply_present(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    s32 value = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_PRESENT);
    int pres_val = (value & SYS_FPGA_REG_BIT(nr % 2)) ? 1 : 0;

    return sprintf(buf, "%d\n", (nr / 2) ? !pres_val : pres_val);
}

/* read power supply ac status */
static ssize_t show_power_supply_ac_status(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    s32 value_present = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_PRESENT);
    s32 value;

    int psu_idx = nr % 2;
    int pres_val = (value_present & SYS_FPGA_REG_BIT(psu_idx)) ? 1 : 0;
    if( pres_val ){
        return sprintf(buf, "%d\n", 0);
    }
 
    value = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_PSU_STATUS);

    return sprintf(buf, "%d\n", (value & SYS_FPGA_REG_BIT(nr)) ? 1 : 0);
}

/* read power supply status1 */
static ssize_t show_power_supply_status(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    s32 value_status = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_PRESENT);

    int psu_idx = nr % 2;
    int pres_val = (value_status & SYS_FPGA_REG_BIT(psu_idx)) ? 1 : 0;
    if( pres_val ){
        return sprintf(buf, "%d\n", 0);
    }

    return sprintf(buf, "%d\n", (value_status & SYS_FPGA_REG_BIT(nr)) ? 1 : 0);
}

/* read temperature label */
static ssize_t show_temp_label(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;

    switch( nr ){
    case 0:
        return snprintf(buf, FPGA_CMD_GET_BUFFER_LEN - 1, "psu0_present\n");
    case 1:
        return snprintf(buf, FPGA_CMD_GET_BUFFER_LEN - 1, "psu1_present\n");
    case 2:
        return snprintf(buf, FPGA_CMD_GET_BUFFER_LEN - 1, "psu0_power_good\n");
    case 3:
        return snprintf(buf, FPGA_CMD_GET_BUFFER_LEN - 1, "psu1_power_good\n");
    }
    return -EINVAL;
}

/* write psu cycle action */
static ssize_t store_psu_cycle(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    s32 value_pwr_cycle;
    unsigned long set_val;

    if (kstrtoul(buf, 10, &set_val))
        return -EINVAL;

    if( (set_val > 1) || (set_val <= 0) ){
        return -EINVAL;
    }

    if( set_val ){
        mutex_lock(&data->update_lock);
        value_pwr_cycle = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_PSU_STATUS);
        value_pwr_cycle |= 0x30;
        i2c_smbus_write_byte_data(client, FPGA_SYS_I2C_REG_PSU_STATUS, value_pwr_cycle);
        mutex_unlock(&data->update_lock);
    }

    return count;
}

/* show bmc_related register */
static ssize_t show_bmc_related(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    s32 value_status = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_BMC_RELATE);

    return sprintf(buf, "%d\n", (value_status & SYS_FPGA_REG_BIT(nr)) ? 1 : 0);
}

/* store bmc_related register */
static ssize_t store_bmc_related(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    int bmc_status = 1;
    s32 value = 0;

    if( !strncmp(buf, "0", 1) )
        bmc_status = 0;
    else if( !strncmp(buf, "1", 1) )
        bmc_status = 1;
    else
        return -EINVAL;

    mutex_lock(&data->update_lock);
    value = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_BMC_RELATE);
    if( (check_same_value(bmc_status, value, nr)) == 1 ) {
        mutex_unlock(&data->update_lock);
        return count;
    }
    i2c_smbus_write_byte_data(client, FPGA_SYS_I2C_REG_BMC_RELATE,
            set_reg_value(bmc_status, value, nr));
    mutex_unlock(&data->update_lock);
    return count;
}

/* show e-fuse ctrl register bit */
static ssize_t show_e_fuse_ctrl(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    s32 value_e_fuse = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_E_FUSE_CTRL);

    return sprintf(buf, "%d\n", (value_e_fuse & SYS_FPGA_REG_BIT(nr)) ? 1 : 0);
}

/* store e-fuse ctrl register bit */
static ssize_t store_e_fuse_ctrl(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    int bmc_status = 1;
    s32 value = 0;

    if( !strncmp(buf, "0", 1) )
        bmc_status = 0;
    else if( !strncmp(buf, "1", 1) )
        bmc_status = 1;
    else
        return -EINVAL;

    mutex_lock(&data->update_lock);
    value = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_E_FUSE_CTRL);
    if( (check_same_value(bmc_status, value, nr)) == 1 ) {
        mutex_unlock(&data->update_lock);
        return count;
    }
  /*
    nr = 7, com_e enable/disable
            when com_e is disabled, console will not resply if it is connected to CPU;
            so we need to switch console to BMC when com_e is disabled,
            and change back to original state when com_e is enabled.
   */
    if( nr == FPGA_FUSE_CTRL_COM_E_EN_BIT ){
        s32 hotkey_value = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_UART_SELECT);
        /* bit 0: uart_select; 0->BMC, 1->CPU
           bit 2: enable_hotkey; 0->disable hotkey, register bit 0 controlled, 1->enable hotkey
         */
        hotkey_value &= ~(FPGA_UART_SELECT_HOTKEY | FPGA_UART_SELECT_CONNECT_CPU);
        if( bmc_status ){
            hotkey_value |= (FPGA_UART_SELECT_HOTKEY | FPGA_UART_SELECT_CONNECT_CPU);
        }
        i2c_smbus_write_byte_data(client, FPGA_SYS_I2C_REG_UART_SELECT, hotkey_value);
    }
    i2c_smbus_write_byte_data(client, FPGA_SYS_I2C_REG_E_FUSE_CTRL,
            set_reg_value(bmc_status, value, nr));
    mutex_unlock(&data->update_lock);
    return count;
}

/* show Power-Off register bit */
static ssize_t show_power_off(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    s32 value_e_fuse = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_POWER_OFF);

    return sprintf(buf, "%d\n", (value_e_fuse & SYS_FPGA_REG_BIT(nr)) ? 1 : 0);
}

/* store e-fuse ctrl register bit */
static ssize_t store_power_off(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    int bmc_status = 1;
    s32 value = 0;

    if( !strncmp(buf, "0", 1) )
        bmc_status = 0;
    else if( !strncmp(buf, "1", 1) )
        bmc_status = 1;
    else
        return -EINVAL;

    mutex_lock(&data->update_lock);
    value = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_POWER_OFF);
    if( (check_same_value(bmc_status, value, nr)) == 1 ) {
        mutex_unlock(&data->update_lock);
        return count;
    }
    i2c_smbus_write_byte_data(client, FPGA_SYS_I2C_REG_POWER_OFF,
            set_reg_value(bmc_status, value, nr));
    mutex_unlock(&data->update_lock);
    return count;
}

/* show fpga reset3 register bit */
static ssize_t show_sys_fpga_reset3(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    s32 value_e_fuse = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_RESET3);

    return sprintf(buf, "%d\n", (value_e_fuse & SYS_FPGA_REG_BIT(nr)) ? 1 : 0);
}

/* store fpga reset3 register bit */
static ssize_t store_sys_fpga_reset3(struct device *dev, struct device_attribute *attr,
                const char *buf, size_t count)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    int bmc_status = 1;
    s32 value = 0;

    if( !strncmp(buf, "0", 1) )
        bmc_status = 0;
    else if( !strncmp(buf, "1", 1) )
        bmc_status = 1;
    else
        return -EINVAL;

    mutex_lock(&data->update_lock);
    value = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_RESET3);
    if( (check_same_value(bmc_status, value, nr)) == 1 ) {
        mutex_unlock(&data->update_lock);
        return count;
    }
    i2c_smbus_write_byte_data(client, FPGA_SYS_I2C_REG_RESET3,
            set_reg_value(bmc_status, value, nr));
    mutex_unlock(&data->update_lock);
    return count;
}

/* store fpga warm reset mac related register */
static ssize_t store_sys_fpga_warm_reset_mac(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    s32 value_mac_reset;
    unsigned long set_val;

    if (kstrtoul(buf, 10, &set_val))
        return -EINVAL;

    if( (set_val > 1) || (set_val < 0) ){
        return -EINVAL;
    }

    mutex_lock(&data->update_lock);
    value_mac_reset = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_RESET3);
    if( set_val ){
        value_mac_reset |= 0x9;
    }
    else{
        value_mac_reset &= ~0x9;
    }
    i2c_smbus_write_byte_data(client, FPGA_SYS_I2C_REG_RESET3, value_mac_reset);
    mutex_unlock(&data->update_lock);

    return count;
}

/* read misc2 register */
static ssize_t show_regster_misc2(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    s32 value_misc2 = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_MISC2);

    return sprintf(buf, "%d\n", (value_misc2 & SYS_FPGA_REG_BIT(nr)) ? 1 : 0);
}

/* write misc2 register */
static ssize_t store_regster_misc2(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    s32 value_misc2;
    unsigned long set_val;

    if (kstrtoul(buf, 10, &set_val))
        return -EINVAL;

    if( (set_val > 1) || (set_val < 0) ){
        return -EINVAL;
    }

    mutex_lock(&data->update_lock);
    value_misc2 = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_MISC2);
    if( (check_same_value(set_val, value_misc2, nr)) == 1 ) {
        mutex_unlock(&data->update_lock);
        return count;
    }
    i2c_smbus_write_byte_data(client, FPGA_SYS_I2C_REG_MISC2,
            set_reg_value(set_val, value_misc2, nr));
    mutex_unlock(&data->update_lock);

    return count;
}

/* show fpga bmc related register bits */
static ssize_t show_fpga_uart_select(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;

    s32 value = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_UART_SELECT);

    return sprintf(buf, "%d\n", (value & SYS_FPGA_REG_BIT(nr)) ? 1 : 0);
}

/* store fpga bmc related register bits */
static ssize_t store_fpga_uart_select(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
    int nr = sensor_attr->index;
    int bmc_status = 1;
    s32 value = 0;

    if( !strncmp(buf, "0", 1) )
        bmc_status = 0;
    else if( !strncmp(buf, "1", 1) )
        bmc_status = 1;
    else
        return -EINVAL;

    mutex_lock(&data->update_lock);
    value = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_UART_SELECT);
    if( (check_same_value(bmc_status, value, nr)) == 1 ) {
        mutex_unlock(&data->update_lock);
        return count;
    }
    i2c_smbus_write_byte_data(client, FPGA_SYS_I2C_REG_UART_SELECT,
            set_reg_value(bmc_status, value, nr));
    mutex_unlock(&data->update_lock);
    return count;
}

/* store fpga sol related register bits */
static ssize_t store_fpga_sol_enable(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    int sol_status = 1;
    s32 value = 0;

    if( !strncmp(buf, "0", 1) )
        sol_status = 0;
    else if( !strncmp(buf, "1", 1) )
        sol_status = 1;
    else
        return -EINVAL;

    mutex_lock(&data->update_lock);
    value = i2c_smbus_read_byte_data(client, FPGA_SYS_I2C_REG_UART_SELECT);
    value &= 0xF0;
    if( sol_status ){
        value |= 0x0C;
    }
    else{
        value |= 0x05;
    }
    i2c_smbus_write_byte_data(client, FPGA_SYS_I2C_REG_UART_SELECT, value);
    mutex_unlock(&data->update_lock);

    return count;
}

/* read access register offset */
static ssize_t show_register_offset(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    int ret;

    ret = snprintf(buf, FPGA_CMD_GET_BUFFER_LEN-1, "0x%02x\n", data->reg_offset);
    return ret;
}

/* write access register offset */
static ssize_t store_register_offset(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    unsigned long reg_val;

    if (kstrtoul(buf, 16, &reg_val))
        return -EINVAL;

    if( (reg_val > 255) || (reg_val < 0) ){
        return -EINVAL;
    }

    mutex_lock(&data->update_lock);
    data->reg_offset = reg_val;
    mutex_unlock(&data->update_lock);

    return count;
}

/* read access register value */
static ssize_t show_register_value(struct device *dev, struct device_attribute *attr,
        char *buf)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    int ret;
    s32 value = i2c_smbus_read_byte_data(client, data->reg_offset);

    ret = snprintf(buf, FPGA_CMD_GET_BUFFER_LEN-1, "0x%02x\n", value);

    return ret;
}

/* write access register value */
static ssize_t store_register_value(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    struct ec_sys_fpga_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    unsigned long reg_val;

    if (kstrtoul(buf, 16, &reg_val))
        return -EINVAL;

    if( (reg_val > 255) || (reg_val < 0) ){
        return -EINVAL;
    }

    mutex_lock(&data->update_lock);
    i2c_smbus_write_byte_data(client, data->reg_offset, reg_val);
    mutex_unlock(&data->update_lock);

    return count;
}

static struct sensor_device_attribute fpga_sys_bd_info[] = {
       SENSOR_ATTR(board_ver, S_IRUGO,
               show_fpga_bd, NULL, 0),
};

static struct sensor_device_attribute fpga_sys_fpga_vers[] = {
       SENSOR_ATTR(cpld_version, S_IRUGO,
               show_fpga_version, NULL, 0),
};

static struct sensor_device_attribute fpga_sys_psu_present[] = {
       SENSOR_ATTR(psu1_present, S_IRUGO,
               show_power_supply_present, NULL, 1),
       SENSOR_ATTR(psu2_present, S_IRUGO,
               show_power_supply_present, NULL, 0),
};

static struct sensor_device_attribute fpga_sys_psu_status[] = {
       SENSOR_ATTR(psu1_acok, S_IRUGO,
               show_power_supply_ac_status, NULL, 1),
       SENSOR_ATTR(psu2_acok, S_IRUGO,
               show_power_supply_ac_status, NULL, 0),
       SENSOR_ATTR(psu1_power_good, S_IRUGO,
               show_power_supply_status, NULL, 3),
       SENSOR_ATTR(psu2_power_good, S_IRUGO,
               show_power_supply_status, NULL, 2),
};

static struct sensor_device_attribute fpga_sys_psu_cycle[] = {
       SENSOR_ATTR(psu_cycle, S_IWUSR,
               NULL, store_psu_cycle, 0),
};

static struct sensor_device_attribute fpga_sys_bmc_related[] = {
       SENSOR_ATTR(bmc_reset, S_IWUSR,
               NULL, store_bmc_related, 6),
       SENSOR_ATTR(uart0_direction, S_IRUGO | S_IWUSR,
               show_bmc_related, store_bmc_related, 7),
};

static struct sensor_device_attribute fpga_sys_e_fuse_ctrl[] = {
       SENSOR_ATTR(com_e_enable, S_IRUGO | S_IWUSR,
               show_e_fuse_ctrl, store_e_fuse_ctrl, 7),
};

static struct sensor_device_attribute fpga_sys_power_off[] = {
       SENSOR_ATTR(mainboard_shutdown, S_IRUGO | S_IWUSR,
               show_power_off, store_power_off, 0),
};

static struct sensor_device_attribute fpga_sys_temp[] = {
       SENSOR_ATTR(temp1_input, S_IRUGO,
                show_power_supply_present, NULL, 3),
       SENSOR_ATTR(temp2_input, S_IRUGO,
                show_power_supply_present, NULL, 2),
       SENSOR_ATTR(temp3_input, S_IRUGO,
                show_power_supply_status, NULL, 3),
       SENSOR_ATTR(temp4_input, S_IRUGO,
                show_power_supply_status, NULL, 2),
};

static struct sensor_device_attribute fpga_sys_reset3[] = {
       SENSOR_ATTR(reset_mac_spi, S_IRUGO | S_IWUSR,
               show_sys_fpga_reset3, store_sys_fpga_reset3, 0),
       SENSOR_ATTR(reset_manu, S_IRUGO | S_IWUSR,
               show_sys_fpga_reset3, store_sys_fpga_reset3, 2),
       SENSOR_ATTR(reset_mac_pcie, S_IRUGO | S_IWUSR,
               show_sys_fpga_reset3, store_sys_fpga_reset3, 3),
       SENSOR_ATTR(cold_reset_mac, S_IRUGO | S_IWUSR,
               show_sys_fpga_reset3, store_sys_fpga_reset3, 5),
       SENSOR_ATTR(warm_reset_mac, S_IWUSR,
               NULL, store_sys_fpga_warm_reset_mac, 0),
};

static struct sensor_device_attribute fpga_sys_temp_label[] = {
       SENSOR_ATTR(temp1_label, S_IRUGO,
                show_temp_label, NULL, 0),
       SENSOR_ATTR(temp2_label, S_IRUGO,
                show_temp_label, NULL, 1),
       SENSOR_ATTR(temp3_label, S_IRUGO,
                show_temp_label, NULL, 2),
       SENSOR_ATTR(temp4_label, S_IRUGO,
                show_temp_label, NULL, 3),
};

static struct sensor_device_attribute fpga_sys_misc2[] = {
       SENSOR_ATTR(select_fancpld, S_IRUGO | S_IWUSR,
               show_regster_misc2, store_regster_misc2, 2),
};

static struct sensor_device_attribute fpga_sys_uart_select[] = {
       SENSOR_ATTR(uart_select, S_IRUGO | S_IWUSR,
               show_fpga_uart_select, store_fpga_uart_select, 0),
       SENSOR_ATTR(enable_hotkey, S_IRUGO | S_IWUSR,
               show_fpga_uart_select, store_fpga_uart_select, 2),
       SENSOR_ATTR(host_uart, S_IRUGO | S_IWUSR,
               show_fpga_uart_select, store_fpga_uart_select, 4),
       SENSOR_ATTR(sol_enable, S_IWUSR,
               NULL, store_fpga_sol_enable, 0),
};

static struct sensor_device_attribute fpga_sys_reg_access[] = {
       SENSOR_ATTR(reg_offset, S_IRUGO | S_IWUSR,
               show_register_offset, store_register_offset, 0),
       SENSOR_ATTR(reg_value, S_IRUGO | S_IWUSR,
               show_register_value, store_register_value, 1),
};

static struct attribute *ec_sys_fpga_attributes[] = {
    &fpga_sys_bd_info[0].dev_attr.attr,
    &fpga_sys_fpga_vers[0].dev_attr.attr,
    &fpga_sys_psu_present[0].dev_attr.attr,
    &fpga_sys_psu_present[1].dev_attr.attr,
    &fpga_sys_psu_status[0].dev_attr.attr,
    &fpga_sys_psu_status[1].dev_attr.attr,
    &fpga_sys_psu_status[2].dev_attr.attr,
    &fpga_sys_psu_status[3].dev_attr.attr,
    &fpga_sys_psu_cycle[0].dev_attr.attr,
    &fpga_sys_bmc_related[0].dev_attr.attr,
    &fpga_sys_bmc_related[1].dev_attr.attr,
    &fpga_sys_e_fuse_ctrl[0].dev_attr.attr,
    &fpga_sys_power_off[0].dev_attr.attr,
    &fpga_sys_reset3[0].dev_attr.attr,
    &fpga_sys_reset3[1].dev_attr.attr,
    &fpga_sys_reset3[2].dev_attr.attr,
    &fpga_sys_reset3[3].dev_attr.attr,
    &fpga_sys_reset3[4].dev_attr.attr,
    &fpga_sys_temp[0].dev_attr.attr,
    &fpga_sys_temp[1].dev_attr.attr,
    &fpga_sys_temp[2].dev_attr.attr,
    &fpga_sys_temp[3].dev_attr.attr,
    &fpga_sys_temp_label[0].dev_attr.attr,
    &fpga_sys_temp_label[1].dev_attr.attr,
    &fpga_sys_temp_label[2].dev_attr.attr,
    &fpga_sys_temp_label[3].dev_attr.attr,
    &fpga_sys_misc2[0].dev_attr.attr,
    &fpga_sys_uart_select[0].dev_attr.attr,
    &fpga_sys_uart_select[1].dev_attr.attr,
    &fpga_sys_uart_select[2].dev_attr.attr,
    &fpga_sys_uart_select[3].dev_attr.attr,
    &fpga_sys_reg_access[0].dev_attr.attr,
    &fpga_sys_reg_access[1].dev_attr.attr,
    NULL
};

static const struct attribute_group ec_sys_fpga_group = {
	.attrs = ec_sys_fpga_attributes,
};
__ATTRIBUTE_GROUPS(ec_sys_fpga);

static int ec_sys_fpga_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct ec_sys_fpga_data *data;
	struct device *dev = &client->dev;
	struct device *hwmon_dev;

	data = devm_kzalloc(&client->dev, sizeof(struct ec_sys_fpga_data),
			GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->client = client;
	mutex_init(&data->update_lock);

	/* Everything is ready, now register the working device */
	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name, data, ec_sys_fpga_groups);
	if (IS_ERR(hwmon_dev)) {
		return PTR_ERR(hwmon_dev);
	}

	dev_info(dev, "%s: sensor '%s'\n", dev_name(hwmon_dev), client->name);
	return 0;
}

static const struct i2c_device_id ec_sys_fpga_id[] = {
	{ "ec_sys_fpga", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ec_sys_fpga_id);

static const struct of_device_id ec_sys_fpga_of_match[] = {
	{ .compatible = "edge-core,ec_system_fpga" },
	{}
};
MODULE_DEVICE_TABLE(of, ec_sys_fpga_of_match);

static struct i2c_driver ec_sys_fpga_driver = {
	.class          = I2C_CLASS_HWMON,
	.driver = {
		.name = "ec_sys_fpga_drv",
		.of_match_table = ec_sys_fpga_of_match,
	},
	.probe          = ec_sys_fpga_probe,
	.id_table       = ec_sys_fpga_id,
};

module_i2c_driver(ec_sys_fpga_driver);
MODULE_AUTHOR("Simon Fan <simon_fan@edge-core.com>");
MODULE_DESCRIPTION("Edge-Core Network System FPGA driver");
MODULE_LICENSE("GPL");
