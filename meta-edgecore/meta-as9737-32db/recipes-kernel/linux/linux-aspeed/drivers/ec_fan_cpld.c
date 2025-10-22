/*
 * ec_fan_cpld.c - Part of lm_sensors, Linux kernel modules for hardware
 *             monitoring
 *
 * Copyright (C) 2019-2029 Simon Fannin <simon_fan@edge-core.com>
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

#define CPLD_CMD_GET_BUFFER_LEN                64
#define NUMBER_OF_FANIN         6
#define NUMBER_OF_PWM           2

#define CPLD_TEMP_MAX    (255*1000)
#define CPLD_TEMP_MIN    (-127*1000)

#define CPLD_FAN_I2C_REG_BD_INFO	0x00
#define CPLD_FAN_I2C_REG_CPLD_VER_MAJ	0x01
#define CPLD_FAN_I2C_REG_PRESENT        0x0F
#define CPLD_FAN_REG_FANTRAY_BIT(x) (1 << (x))

#define CPLD_FAN_I2C_REG_FAN_DIR        0x10
#define FAN_PWM_FRONT      0x11
#define FAN_PWM_REAR       0x49

#define FAN1_VEND_ID       0x43
#define FAN2_VEND_ID       0x44
#define FAN3_VEND_ID       0x45
#define FAN4_VEND_ID       0x46
#define FAN5_VEND_ID       0x47
#define FAN6_VEND_ID       0x48
    #define  VENDOR_AVC     4
    #define  VENDOR_Nidec   6

#define FAN1_TACH_F     0x12
#define FAN1_TACH_B     0x22

#define FAN2_TACH_F     0x13
#define FAN2_TACH_B     0x23

#define FAN3_TACH_F     0x14
#define FAN3_TACH_B     0x24

#define FAN4_TACH_F     0x15
#define FAN4_TACH_B     0x25

#define FAN5_TACH_F     0x16
#define FAN5_TACH_B     0x26

#define FAN6_TACH_F     0x17
#define FAN6_TACH_B     0x27

#define CPLD_FAN_I2C_REG_POWER_GOOD        0x2F

/*toby modify: rpm define*/
/*
	function : RPM = 30*N/T
	N = read value for 0x90 ~ 0x9C
	T = base time = 400ms
RPM = N* (30*1000)/400 = N*75
*/
#define CPLD_FAN_CONSTANT 200

struct ec_fan_cpld_data {
	struct i2c_client *client;
	struct mutex update_lock;

	char valid;                     /* !=0 if following fields are valid */
	unsigned long last_updated;     /* In jiffies */

	u8 reg_offset;
	int  mac_temp;
	int  transceiver_temp;
	time64_t temp_stamptime;
	int  transceiver_index;
};

/* read BD Information */
static ssize_t show_cpld_bd(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;
    s32 value = i2c_smbus_read_byte_data(client, CPLD_FAN_I2C_REG_BD_INFO);

    return sprintf(buf, "0x%02x\n", (value >> 4) & 0x7);
}

/* read CPLD version */
static ssize_t show_cpld_version(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
    struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
    struct i2c_client *client = data->client;

    s32 value_maj = i2c_smbus_read_byte_data(client, CPLD_FAN_I2C_REG_CPLD_VER_MAJ);

    return sprintf(buf, "0x%02x\n", value_maj & 0xff);
}

/* read front fan speed in rpm */
static ssize_t show_speed(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int reg = sensor_attr->index;
	s32 value = i2c_smbus_read_byte_data(client, reg);
	int  rpm = (value & 0xff) * CPLD_FAN_CONSTANT;

	return sprintf(buf, "%d\n", rpm);
}

/* read front fan label */
static ssize_t show_label(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	return sprintf(buf, "fan%d %s\n", nr % 6, !(nr / 6) ? "front" : "rear");
}

/* read access register offset */
static ssize_t show_register_offset(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
       struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
       int ret;

       ret = snprintf(buf, CPLD_CMD_GET_BUFFER_LEN-1, "0x%02x\n", data->reg_offset);
       return ret;
}

/* write access register offset */
static ssize_t store_register_offset(struct device *dev, struct device_attribute *attr,
               const char *buf, size_t count)
{
       struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
       unsigned long reg_val;

       if (kstrtoul(buf, 16, &reg_val))
               return -EINVAL;

       if( (reg_val > 255) || (reg_val < 0) ){
               return -EINVAL;
       }

       data->reg_offset = reg_val;

       return count;
}

/* read access register value */
static ssize_t show_register_value(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
       struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
       struct i2c_client *client = data->client;
       int ret;
       s32 value = i2c_smbus_read_byte_data(client, data->reg_offset);

       ret = snprintf(buf, CPLD_CMD_GET_BUFFER_LEN-1, "0x%02x\n", value);

       return ret;
}

/* write access register value */
static ssize_t store_register_value(struct device *dev, struct device_attribute *attr,
               const char *buf, size_t count)
{
       struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
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

/* read FAN present */
static ssize_t show_fan_present(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
        struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
        struct i2c_client *client = data->client;
        struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
        int nr = sensor_attr->index;
        s32 value = i2c_smbus_read_byte_data(client, CPLD_FAN_I2C_REG_PRESENT);

	// convert => present: 1, unpresent: 0
        return sprintf(buf, "%d\n", (value & CPLD_FAN_REG_FANTRAY_BIT(nr)) ? 0 : 1);
}

/* read FAN mask present */
static ssize_t show_fan_mask_present(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
        struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
        struct i2c_client *client = data->client;
        s32 value = i2c_smbus_read_byte_data(client, CPLD_FAN_I2C_REG_PRESENT);

        // convert => present: 1, unpresent: 0
        return sprintf(buf, "0x%x\n", ((~value) & ((1 << NUMBER_OF_FANIN) - 1) ));
}

/* read FAN power good */
static ssize_t show_fan_power_good(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
        struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
        struct i2c_client *client = data->client;
        struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
        int nr = sensor_attr->index;
        s32 value = i2c_smbus_read_byte_data(client, CPLD_FAN_I2C_REG_POWER_GOOD);

        return sprintf(buf, "%d\n", (value & CPLD_FAN_REG_FANTRAY_BIT(nr)) ? 1 : 0);
}

/* read FAN mask power good status */
static ssize_t show_fan_mask_pgd(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
        struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
        struct i2c_client *client = data->client;
        s32 value = i2c_smbus_read_byte_data(client, CPLD_FAN_I2C_REG_POWER_GOOD);

        return sprintf(buf, "0x%x\n", (value & ((1 << NUMBER_OF_FANIN) - 1) ));
}

/* read fan direction */
static ssize_t show_fan_dir(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
        struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
        struct i2c_client *client = data->client;
        struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
        int nr = sensor_attr->index;
        s32 value = i2c_smbus_read_byte_data(client, CPLD_FAN_I2C_REG_FAN_DIR);

        return sprintf(buf, "%d\n", (value & CPLD_FAN_REG_FANTRAY_BIT(nr)) ? 1 : 0);
}

/* read FAN mask direction */
static ssize_t show_fan_mask_dir(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
        struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
        struct i2c_client *client = data->client;
        s32 value = i2c_smbus_read_byte_data(client, CPLD_FAN_I2C_REG_FAN_DIR);

        return sprintf(buf, "0x%x\n", (value & ((1 << NUMBER_OF_FANIN) - 1) ));
}

/* read indivisual FAN PWM ( register 0 ~ 255 ) value */
static ssize_t show_pwm(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
        struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
        struct i2c_client *client = data->client;
        unsigned int reg_val;
        s32 value = i2c_smbus_read_byte_data(client, FAN_PWM_FRONT);
        reg_val= value & 0xff;
        return sprintf(buf, "%d\n", reg_val);
}

/* write front & rear PWMs ( register value: 0 ~ 255 ) */
static ssize_t store_pwm(struct device *dev, struct device_attribute *attr,
               const char *buf, size_t count)
{
        struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
        struct i2c_client *client = data->client;
        unsigned long pwm_val;

        if (kstrtoul(buf, 10, &pwm_val))
                return -EINVAL;

        if (pwm_val > 255 )
                return -EINVAL;

        mutex_lock(&data->update_lock);
        i2c_smbus_write_byte_data(client, FAN_PWM_FRONT, pwm_val);

        i2c_smbus_write_byte_data(client, FAN_PWM_REAR, pwm_val);
        mutex_unlock(&data->update_lock);

        return count;
}

/* read fan vendor Id */
static ssize_t show_vendor(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
        struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
        struct i2c_client *client = data->client;
        struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
        int vendor_reg = sensor_attr->index;
        s32 value = 0;
        int vendor_id = 0, ret = 0;
        char vendor_name[16];

        value = i2c_smbus_read_byte_data(client, vendor_reg);
        vendor_id = value & 7;
        switch( vendor_id ) {
        case VENDOR_AVC:
            strcpy(vendor_name, "AVC");
            break;
        case VENDOR_Nidec:
            strcpy(vendor_name, "Nidec");
            break;
        default:
            ret = sprintf(vendor_name, "unknown(id:%d)", vendor_id);
            vendor_name[ret] = '\0';
            break;
        }

        return sprintf(buf, "%s\n", vendor_name);
}

/* read thermal plan temperatures */
static ssize_t show_thermal_plan_temp(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
        struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
        struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
        int nr = sensor_attr->index;

        switch( nr ){
        case 0:
            return sprintf(buf, "%d\n", data->mac_temp);
        case 1:
            return sprintf(buf, "%d\n", data->transceiver_temp);
        }
        return -EINVAL;
}

/* write thermal plan temperatures */
static ssize_t store_thermal_plan_temp(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
        struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
        int nr = 0;
        int temp_val, qsfp_index;
        char *token, *cur = buf;
        const char *delimit = " \t\n;,";

	do{
            token = strsep(&cur, delimit);

            switch( nr ){
            case 0:
                if( token == NULL )
                    return -EINVAL;
                if (kstrtoint(token, 10, &temp_val))
                    return -EINVAL;
                if( (temp_val > CPLD_TEMP_MAX) || (temp_val < CPLD_TEMP_MIN) ){
                    return -EINVAL;
                }
                data->mac_temp = temp_val;
                break;
            case 1:
                if( token == NULL )
                    return -EINVAL;
                if (kstrtoint(token, 10, &temp_val))
                    return -EINVAL;
                if( (temp_val > CPLD_TEMP_MAX) || (temp_val < CPLD_TEMP_MIN) ){
                    return -EINVAL;
                }
                data->transceiver_temp = temp_val;
                break;
            case 2:
                if( token != NULL ){
                    if (kstrtoint(token, 10, &qsfp_index)){
                        data->transceiver_index = -1;
                    }
                    else{
                        data->transceiver_index = qsfp_index;
                    }
                }
                else{
                    data->transceiver_index = -1;
                }
                break;
            }
        } while( ++nr < 3 );

        data->temp_stamptime = ktime_get_boottime_seconds();
        return count;
}

/* read temp set info */
static ssize_t show_temp_info(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
        struct ec_fan_cpld_data *data = dev_get_drvdata(dev);
        struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
        int nr = sensor_attr->index;

        switch( nr ){
        case 0:
            return sprintf(buf, "%d\n", data->temp_stamptime);
        case 1:
            return sprintf(buf, "%d\n", data->transceiver_index);
        }
        return -EINVAL;
}

static ssize_t show_temp_label(struct device *dev, struct device_attribute *attr,
                       char *buf)
{
        struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
        int nr = sensor_attr->index;

        switch( nr ){
        case 0:
            return snprintf(buf, CPLD_CMD_GET_BUFFER_LEN - 1, "MAC Temperature\n");
        case 1:
            return snprintf(buf, CPLD_CMD_GET_BUFFER_LEN - 1, "Transceiver Temperature\n");
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
            return snprintf(buf, CPLD_CMD_GET_BUFFER_LEN - 1, "fan%d_present\n", nr - 2);
        }
        return -EINVAL;
}

static struct sensor_device_attribute cpld_fan_bd_info[] = {
	SENSOR_ATTR(board_ver, S_IRUGO,
		show_cpld_bd, NULL, 0),
};

static struct sensor_device_attribute cpld_fan_cpld_vers[] = {
	SENSOR_ATTR(cpld_version, S_IRUGO,
		show_cpld_version, NULL, 0),
};

static struct sensor_device_attribute cpld_fan_speed[] = {
	SENSOR_ATTR(fan1_input, S_IRUGO,
		show_speed, NULL, FAN1_TACH_F),
	SENSOR_ATTR(fan2_input, S_IRUGO,
		show_speed, NULL, FAN2_TACH_F),
	SENSOR_ATTR(fan3_input, S_IRUGO,
		show_speed, NULL, FAN3_TACH_F),
	SENSOR_ATTR(fan4_input, S_IRUGO,
		show_speed, NULL, FAN4_TACH_F),
	SENSOR_ATTR(fan5_input, S_IRUGO,
		show_speed, NULL, FAN5_TACH_F),
	SENSOR_ATTR(fan6_input, S_IRUGO,
		show_speed, NULL, FAN6_TACH_F),
	SENSOR_ATTR(fan7_input, S_IRUGO,
		show_speed, NULL, FAN1_TACH_B),
	SENSOR_ATTR(fan8_input, S_IRUGO,
		show_speed, NULL, FAN2_TACH_B),
	SENSOR_ATTR(fan9_input, S_IRUGO,
		show_speed, NULL, FAN3_TACH_B),
	SENSOR_ATTR(fan10_input, S_IRUGO,
		show_speed, NULL, FAN4_TACH_B),
	SENSOR_ATTR(fan11_input, S_IRUGO,
		show_speed, NULL, FAN5_TACH_B),
	SENSOR_ATTR(fan12_input, S_IRUGO,
		show_speed, NULL, FAN6_TACH_B),
};

static struct sensor_device_attribute cpld_fan_label[] = {
	SENSOR_ATTR(fan1_label, S_IRUGO,
		show_label, NULL, 0),
	SENSOR_ATTR(fan2_label, S_IRUGO,
		show_label, NULL, 1),
	SENSOR_ATTR(fan3_label, S_IRUGO,
		show_label, NULL, 2),
	SENSOR_ATTR(fan4_label, S_IRUGO,
		show_label, NULL, 3),
	SENSOR_ATTR(fan5_label, S_IRUGO,
		show_label, NULL, 4),
	SENSOR_ATTR(fan6_label, S_IRUGO,
		show_label, NULL, 5),
	SENSOR_ATTR(fan7_label, S_IRUGO,
		show_label, NULL, 6),
	SENSOR_ATTR(fan8_label, S_IRUGO,
		show_label, NULL, 7),
	SENSOR_ATTR(fan9_label, S_IRUGO,
		show_label, NULL, 8),
	SENSOR_ATTR(fan10_label, S_IRUGO,
		show_label, NULL, 9),
	SENSOR_ATTR(fan11_label, S_IRUGO,
		show_label, NULL, 10),
	SENSOR_ATTR(fan12_label, S_IRUGO,
		show_label, NULL, 11),
};

static struct sensor_device_attribute cpld_fan_reg_access[] = {
       SENSOR_ATTR(reg_offset, S_IRUGO | S_IWUSR,
               show_register_offset, store_register_offset, 0),
       SENSOR_ATTR(reg_value, S_IRUGO | S_IWUSR,
               show_register_value, store_register_value, 1),
};

static struct sensor_device_attribute cpld_fan_present[] = {
	SENSOR_ATTR(fan1_Presence, S_IRUGO,
		show_fan_present, NULL, 0),
	SENSOR_ATTR(fan2_Presence, S_IRUGO,
		show_fan_present, NULL, 1),
	SENSOR_ATTR(fan3_Presence, S_IRUGO,
		show_fan_present, NULL, 2),
	SENSOR_ATTR(fan4_Presence, S_IRUGO,
		show_fan_present, NULL, 3),
	SENSOR_ATTR(fan5_Presence, S_IRUGO,
		show_fan_present, NULL, 4),
	SENSOR_ATTR(fan6_Presence, S_IRUGO,
		show_fan_present, NULL, 5),
        SENSOR_ATTR(mask_Presence, S_IRUGO,
                show_fan_mask_present, NULL, 0),
};

static struct sensor_device_attribute cpld_fan_pw_gd[] = {
	SENSOR_ATTR(fan1_pgd, S_IRUGO,
		show_fan_power_good, NULL, 0),
	SENSOR_ATTR(fan2_pgd, S_IRUGO,
		show_fan_power_good, NULL, 1),
	SENSOR_ATTR(fan3_pgd, S_IRUGO,
		show_fan_power_good, NULL, 2),
	SENSOR_ATTR(fan4_pgd, S_IRUGO,
		show_fan_power_good, NULL, 3),
	SENSOR_ATTR(fan5_pgd, S_IRUGO,
		show_fan_power_good, NULL, 4),
	SENSOR_ATTR(fan6_pgd, S_IRUGO,
		show_fan_power_good, NULL, 5),
        SENSOR_ATTR(mask_pgd, S_IRUGO,
                show_fan_mask_pgd, NULL, 0),
};

static struct sensor_device_attribute cpld_fan_pwm_reg[] = {
	SENSOR_ATTR(pwm_reg, S_IRUGO | S_IWUSR,
		show_pwm, store_pwm, FAN_PWM_FRONT),
};

static struct sensor_device_attribute cpld_fan_dir[] = {
	SENSOR_ATTR(fan1_dir, S_IRUGO,
		show_fan_dir, NULL, 0),
	SENSOR_ATTR(fan2_dir, S_IRUGO,
		show_fan_dir, NULL, 1),
	SENSOR_ATTR(fan3_dir, S_IRUGO,
		show_fan_dir, NULL, 2),
	SENSOR_ATTR(fan4_dir, S_IRUGO,
		show_fan_dir, NULL, 3),
	SENSOR_ATTR(fan5_dir, S_IRUGO,
		show_fan_dir, NULL, 4),
	SENSOR_ATTR(fan6_dir, S_IRUGO,
		show_fan_dir, NULL, 5),
        SENSOR_ATTR(mask_fan_dir, S_IRUGO,
                show_fan_mask_dir, NULL, 0),
};

static struct sensor_device_attribute cpld_fan_vendor[] = {
	SENSOR_ATTR(fan1_vendor, S_IRUGO,
		show_vendor, NULL, FAN1_VEND_ID),
	SENSOR_ATTR(fan2_vendor, S_IRUGO,
		show_vendor, NULL, FAN2_VEND_ID),
	SENSOR_ATTR(fan3_vendor, S_IRUGO,
		show_vendor, NULL, FAN3_VEND_ID),
	SENSOR_ATTR(fan4_vendor, S_IRUGO,
		show_vendor, NULL, FAN4_VEND_ID),
	SENSOR_ATTR(fan5_vendor, S_IRUGO,
		show_vendor, NULL, FAN5_VEND_ID),
	SENSOR_ATTR(fan6_vendor, S_IRUGO,
		show_vendor, NULL, FAN6_VEND_ID),
};

static struct sensor_device_attribute cpld_fan_temp_input[] = {
	SENSOR_ATTR(temp1_input, S_IRUGO,
		show_thermal_plan_temp, NULL, 0),
	SENSOR_ATTR(temp2_input, S_IRUGO,
		show_thermal_plan_temp, NULL, 1),
	SENSOR_ATTR(temp3_input, S_IRUGO,
		show_fan_present, NULL, 0),
	SENSOR_ATTR(temp4_input, S_IRUGO,
		show_fan_present, NULL, 1),
	SENSOR_ATTR(temp5_input, S_IRUGO,
		show_fan_present, NULL, 2),
	SENSOR_ATTR(temp6_input, S_IRUGO,
		show_fan_present, NULL, 3),
	SENSOR_ATTR(temp7_input, S_IRUGO,
		show_fan_present, NULL, 4),
	SENSOR_ATTR(temp8_input, S_IRUGO,
		show_fan_present, NULL, 5),
	SENSOR_ATTR(temp_stamp, S_IRUGO,
		show_temp_info, NULL, 0),
	SENSOR_ATTR(temp2_index, S_IRUGO,
		show_temp_info, NULL, 1),
	SENSOR_ATTR(temp_store, S_IWUSR,
		NULL, store_thermal_plan_temp, 0),
};

static struct sensor_device_attribute cpld_fan_temp_label[] = {
	SENSOR_ATTR(temp1_label, S_IRUGO,
		show_temp_label, NULL, 0),
	SENSOR_ATTR(temp2_label, S_IRUGO,
		show_temp_label, NULL, 1),
	SENSOR_ATTR(temp3_label, S_IRUGO,
		show_temp_label, NULL, 2),
	SENSOR_ATTR(temp4_label, S_IRUGO,
		show_temp_label, NULL, 3),
	SENSOR_ATTR(temp5_label, S_IRUGO,
		show_temp_label, NULL, 4),
	SENSOR_ATTR(temp6_label, S_IRUGO,
		show_temp_label, NULL, 5),
	SENSOR_ATTR(temp7_label, S_IRUGO,
		show_temp_label, NULL, 6),
	SENSOR_ATTR(temp8_label, S_IRUGO,
		show_temp_label, NULL, 7),
};

static struct attribute *ec_fan_cpld_attributes[] = {
	&cpld_fan_bd_info[0].dev_attr.attr,
	&cpld_fan_cpld_vers[0].dev_attr.attr,
	&cpld_fan_present[0].dev_attr.attr,
	&cpld_fan_present[1].dev_attr.attr,
	&cpld_fan_present[2].dev_attr.attr,
	&cpld_fan_present[3].dev_attr.attr,
	&cpld_fan_present[4].dev_attr.attr,
	&cpld_fan_present[5].dev_attr.attr,
	&cpld_fan_present[6].dev_attr.attr,
	&cpld_fan_pw_gd[0].dev_attr.attr,
	&cpld_fan_pw_gd[1].dev_attr.attr,
	&cpld_fan_pw_gd[2].dev_attr.attr,
	&cpld_fan_pw_gd[3].dev_attr.attr,
	&cpld_fan_pw_gd[4].dev_attr.attr,
	&cpld_fan_pw_gd[5].dev_attr.attr,
	&cpld_fan_pw_gd[6].dev_attr.attr,
	&cpld_fan_pwm_reg[0].dev_attr.attr,
	&cpld_fan_speed[0].dev_attr.attr,
	&cpld_fan_speed[1].dev_attr.attr,
	&cpld_fan_speed[2].dev_attr.attr,
	&cpld_fan_speed[3].dev_attr.attr,
	&cpld_fan_speed[4].dev_attr.attr,
	&cpld_fan_speed[5].dev_attr.attr,
	&cpld_fan_speed[6].dev_attr.attr,
	&cpld_fan_speed[7].dev_attr.attr,
	&cpld_fan_speed[8].dev_attr.attr,
	&cpld_fan_speed[9].dev_attr.attr,
	&cpld_fan_speed[10].dev_attr.attr,
	&cpld_fan_speed[11].dev_attr.attr,
	&cpld_fan_dir[0].dev_attr.attr,
	&cpld_fan_dir[1].dev_attr.attr,
	&cpld_fan_dir[2].dev_attr.attr,
	&cpld_fan_dir[3].dev_attr.attr,
	&cpld_fan_dir[4].dev_attr.attr,
	&cpld_fan_dir[5].dev_attr.attr,
	&cpld_fan_dir[6].dev_attr.attr,
	&cpld_fan_vendor[0].dev_attr.attr,
	&cpld_fan_vendor[1].dev_attr.attr,
	&cpld_fan_vendor[2].dev_attr.attr,
	&cpld_fan_vendor[3].dev_attr.attr,
	&cpld_fan_vendor[4].dev_attr.attr,
	&cpld_fan_vendor[5].dev_attr.attr,
	&cpld_fan_label[0].dev_attr.attr,
	&cpld_fan_label[1].dev_attr.attr,
	&cpld_fan_label[2].dev_attr.attr,
	&cpld_fan_label[3].dev_attr.attr,
	&cpld_fan_label[4].dev_attr.attr,
	&cpld_fan_label[5].dev_attr.attr,
	&cpld_fan_label[6].dev_attr.attr,
	&cpld_fan_label[7].dev_attr.attr,
	&cpld_fan_label[8].dev_attr.attr,
	&cpld_fan_label[9].dev_attr.attr,
	&cpld_fan_label[10].dev_attr.attr,
	&cpld_fan_label[11].dev_attr.attr,
	&cpld_fan_temp_input[0].dev_attr.attr,
	&cpld_fan_temp_input[1].dev_attr.attr,
	&cpld_fan_temp_input[2].dev_attr.attr,
	&cpld_fan_temp_input[3].dev_attr.attr,
	&cpld_fan_temp_input[4].dev_attr.attr,
	&cpld_fan_temp_input[5].dev_attr.attr,
	&cpld_fan_temp_input[6].dev_attr.attr,
	&cpld_fan_temp_input[7].dev_attr.attr,
	&cpld_fan_temp_input[8].dev_attr.attr,
	&cpld_fan_temp_input[9].dev_attr.attr,
	&cpld_fan_temp_input[10].dev_attr.attr,
	&cpld_fan_temp_label[0].dev_attr.attr,
	&cpld_fan_temp_label[1].dev_attr.attr,
	&cpld_fan_temp_label[2].dev_attr.attr,
	&cpld_fan_temp_label[3].dev_attr.attr,
	&cpld_fan_temp_label[4].dev_attr.attr,
	&cpld_fan_temp_label[5].dev_attr.attr,
	&cpld_fan_temp_label[6].dev_attr.attr,
	&cpld_fan_temp_label[7].dev_attr.attr,
	&cpld_fan_reg_access[0].dev_attr.attr,
	&cpld_fan_reg_access[1].dev_attr.attr,
	NULL
};

static const struct attribute_group ec_fan_cpld_group = {
	.attrs = ec_fan_cpld_attributes,
};
__ATTRIBUTE_GROUPS(ec_fan_cpld);

static int ec_fan_cpld_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct ec_fan_cpld_data *data;
	struct device *dev = &client->dev;
	struct device *hwmon_dev;

	data = devm_kzalloc(&client->dev, sizeof(struct ec_fan_cpld_data),
			GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->client = client;
	mutex_init(&data->update_lock);

	data->mac_temp = CPLD_TEMP_MIN;
	data->transceiver_temp = CPLD_TEMP_MIN;
	data->transceiver_index = -1;

	/* Everything is ready, now register the working device */
	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name, data, ec_fan_cpld_groups);
	if (IS_ERR(hwmon_dev)) {
		return PTR_ERR(hwmon_dev);
	}

	dev_info(dev, "%s: sensor '%s'\n", dev_name(hwmon_dev), client->name);
	return 0;
}

static const struct i2c_device_id ec_fan_cpld_id[] = {
	{ "ec_fan_cpld", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ec_fan_cpld_id);

static const struct of_device_id ec_fan_cpld_of_match[] = {
	{ .compatible = "edge-core,ec_fan_cpld2" },
	{}
};
MODULE_DEVICE_TABLE(of, ec_fan_cpld_of_match);

static struct i2c_driver ec_fan_cpld_driver = {
	.class          = I2C_CLASS_HWMON,
	.driver = {
		.name = "ec_fan_cpld_drv",
		.of_match_table = ec_fan_cpld_of_match,
	},
	.probe          = ec_fan_cpld_probe,
	.id_table       = ec_fan_cpld_id,
};

module_i2c_driver(ec_fan_cpld_driver);
MODULE_AUTHOR("Simon Fan <simon_fan@edge-core.com>");
MODULE_DESCRIPTION("Edge-Core Network CPLD FAN driver");
MODULE_LICENSE("GPL");
