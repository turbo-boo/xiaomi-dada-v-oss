// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi MCA Nuvolta NU1652 wireless receiver runtime driver for Dada.
 *
 * This bring-up implementation deliberately excludes MTP erase/programming
 * and embedded firmware blobs.  It restores the stock runtime transport,
 * presence/IRQ handling, telemetry, voltage control, authentication data,
 * transparent-data path and DT-provided FOD tables used during charging.
 */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/strategy/strategy_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "nuvolta_1652"
#endif

#define NU1652_I2C_RETRIES                  3
#define NU1652_READY_RETRIES                100
#define NU1652_RUNTIME_LEN                  50
#define NU1652_MAX_FOD_GROUPS               20
#define NU1652_MAX_FOD_PAIRS                20
#define NU1652_FOD_FIELDS                   5

#define REG_RX_SENT_CMD                     0x0000
#define REG_RX_SENT_DATA1                   0x0001
#define REG_RX_SENT_DATA2                   0x0002
#define REG_RX_SENT_DATA3                   0x0003
#define REG_RX_SENT_DATA4                   0x0004
#define REG_RX_SENT_DATA5                   0x0005
#define REG_RX_SENT_DATA6                   0x0006
#define REG_RX_REV_CMD                      0x0020
#define REG_RX_REV_DATA1                    0x0021
#define REG_RX_REV_DATA4                    0x0024
#define REG_RX_REV_DATA5                    0x0025
#define REG_RX_POWER_OFF_ERR                0x0028
#define REG_RX_RTX_MODE                     0x002f
#define REG_RX_INT_0                        0x0060
#define REG_RX_INT_2                        0x0062
#define REG_RX_INT_3                        0x0063
#define REG_RX_DATA_INFO                    0x1200
#define REG_RX_SS_VOLTAGE                   0x126a
#define REG_RX_FASTCHG_RESULT               0x1209

#define RX_CMD_START_READ                   0x88
#define RX_CMD_CLEAR_INT                    0x68
#define RX_CMD_SET_RX_VOUT                  0x31
#define RX_CMD_TRANSMIT_PACKET              0x69
#define RX_CMD_FOD_SET                      0x98
#define RX_CMD_RENEGO_SET                   0xa8
#define RX_CMD_ENABLE_REVERSE_FOD           0x23

#define RX_CMD_ENABLE_TX                    0x01
#define RX_CMD_DISABLE_TX                   0x00
#define RX_CMD_BUSY                         0x55

#define RX_CLEAR_INT_LENGTH                 0x02
#define RX_CLEAR_INT_TRIGGER                0x04

#define RX_VOUT_MIN_MV                      4000
#define RX_VOUT_MAX_MV                      19500
#define RX_VOUT_DEFAULT_MV                  6000
#define RX_VOUT_PACKET_LENGTH               0x02
#define RX_VOUT_TRIGGER                     0x04

#define RX_ADAPTER_MIN_MV                   4000
#define RX_ADAPTER_MAX_MV                   30000
#define RX_ADAPTER_DEFAULT_MV               6000
#define RX_ADAPTER_PACKET_LENGTH            0x05
#define RX_ADAPTER_SET_TYPE                 0x02
#define RX_ADAPTER_TRIGGER                  0x07
#define RX_ADAPTER_SET_VOLTAGE_CMD          0x0a
#define RX_TRANSPARENT_3BYTE                0x38

#define RX_RENEGO_LENGTH                    0x01
#define RX_RENEGO_TRIGGER                   0x03

#define RX_REVERSE_FOD_ENABLE               0x01
#define RX_REVERSE_FOD_DISABLE              0x00
#define RX_REVERSE_FOD_GAIN                 94
#define RX_REVERSE_FOD_TRIGGER              0x04
#define RX_REVERSE_FOD_DISABLE_TRIGGER      0x02

#define RX_FOD_CHUNK_PAIRS                  5
#define RX_FOD_BPP_PACKET_LENGTH            0x0d
#define RX_FOD_BPP_TRIGGER                  15

#define RX_RUNTIME_POWER_MODE               3
#define RX_RUNTIME_MAX_POWER                5
#define RX_RUNTIME_ADAPTER_TYPE             7
#define RX_RUNTIME_AUTH                     8
#define RX_RUNTIME_TEMP_L                   14
#define RX_RUNTIME_TEMP_H                   15
#define RX_RUNTIME_IOUT_L                   16
#define RX_RUNTIME_IOUT_H                   17
#define RX_RUNTIME_VRECT_L                  18
#define RX_RUNTIME_VRECT_H                  19
#define RX_RUNTIME_VOUT_L                   20
#define RX_RUNTIME_VOUT_H                   21
#define RX_RUNTIME_TX_ID_L                  26
#define RX_RUNTIME_TX_ID_H                  27
#define RX_RUNTIME_UUID0                    28
#define RX_RUNTIME_UUID1                    29
#define RX_RUNTIME_UUID2                    30
#define RX_RUNTIME_UUID3                    31
#define RX_RUNTIME_RX_DATA_LEN              40
#define RX_RUNTIME_TRANS_LEN                40
#define RX_RUNTIME_TRANS_DATA               41
#define RX_RUNTIME_TRX_ISENSE_L             16
#define RX_RUNTIME_TRX_ISENSE_H             17
#define RX_RUNTIME_TRX_VRECT_L              18
#define RX_RUNTIME_TRX_VRECT_H              19

#define RX_MODE_STATUS_RTX                  0x03
#define RX_OFFSET_THRESHOLD_MV              5000

struct nu1652_fod_pair {
	u8 gain;
	u8 offset;
};

struct nu1652_fod_group {
	u8 type;
	u8 length;
	u32 uuid;
	struct nu1652_fod_pair normal[NU1652_MAX_FOD_PAIRS];
	struct nu1652_fod_pair magnetic[NU1652_MAX_FOD_PAIRS];
	bool valid;
};

struct nu1652_chip {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct mutex io_lock;
	struct mutex data_lock;

	u32 role;
	u32 project_vendor;
	bool support_hall;
	int sleep_gpio;
	int power_good_gpio;
	int rx_irq_gpio;
	int hall_gpio;
	int power_good_irq;
	int rx_irq;
	int hall_irq;

	bool present;
	bool magnetic_case;
	int last_irq;
	int last_trx_mode;
	int vout_setted;
	int fake_rx_offset;
	u8 adapter_type;
	u8 power_mode;
	u8 max_power;
	u8 auth_value;
	u8 uuid[4];

	struct nu1652_fod_group fod[NU1652_MAX_FOD_GROUPS];
	int fod_count;
	struct nu1652_fod_group fod_default;
	struct nu1652_fod_group fod_bpp_plus;
};

static const struct regmap_config nu1652_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xffff,
};

static int nu1652_read(struct nu1652_chip *chip, u16 reg, u8 *val)
{
	unsigned int tmp;
	int i, ret = -EIO;

	if (!val)
		return -EINVAL;

	mutex_lock(&chip->io_lock);
	for (i = 0; i < NU1652_I2C_RETRIES; i++) {
		ret = regmap_read(chip->regmap, reg, &tmp);
		if (!ret) {
			*val = tmp;
			break;
		}
		usleep_range(1000, 1500);
	}
	mutex_unlock(&chip->io_lock);
	return ret;
}

static int nu1652_write(struct nu1652_chip *chip, u16 reg, u8 val)
{
	int i, ret = -EIO;

	mutex_lock(&chip->io_lock);
	for (i = 0; i < NU1652_I2C_RETRIES; i++) {
		ret = regmap_write(chip->regmap, reg, val);
		if (!ret)
			break;
		usleep_range(1000, 1500);
	}
	mutex_unlock(&chip->io_lock);
	return ret;
}

static int nu1652_read_buffer(struct nu1652_chip *chip, u16 reg, u8 *buf,
			       size_t len)
{
	size_t i;
	int ret;

	if (!buf)
		return -EINVAL;
	for (i = 0; i < len; i++) {
		ret = nu1652_read(chip, reg + i, &buf[i]);
		if (ret)
			return ret;
	}
	return 0;
}

static int nu1652_write_buffer(struct nu1652_chip *chip, u16 reg,
				u8 *buf, size_t len)
{
	size_t i;
	int ret;

	if (!buf)
		return -EINVAL;
	for (i = 0; i < len; i++) {
		ret = nu1652_write(chip, reg + i, buf[i]);
		if (ret)
			return ret;
	}
	return 0;
}

static int nu1652_wait_free(struct nu1652_chip *chip, u16 reg)
{
	u8 value = RX_CMD_BUSY;
	int i, ret;

	for (i = 0; i < NU1652_READY_RETRIES; i++) {
		ret = nu1652_read(chip, reg, &value);
		if (ret)
			return ret;
		if (value != RX_CMD_BUSY)
			return 0;
		usleep_range(1000, 1500);
	}
	return -ETIMEDOUT;
}

static int nu1652_prepare_runtime_read(struct nu1652_chip *chip)
{
	int ret;

	ret = nu1652_wait_free(chip, REG_RX_REV_DATA5);
	if (ret)
		return ret;
	ret = nu1652_write(chip, REG_RX_INT_2, RX_CMD_START_READ);
	if (ret)
		return ret;
	return nu1652_wait_free(chip, REG_RX_REV_DATA4);
}

static int nu1652_get_runtime(struct nu1652_chip *chip, u8 *buf, size_t len)
{
	int ret;

	if (!chip->present)
		return -ENODATA;
	if (len > NU1652_RUNTIME_LEN)
		return -EINVAL;

	mutex_lock(&chip->data_lock);
	ret = nu1652_prepare_runtime_read(chip);
	if (!ret)
		ret = nu1652_read_buffer(chip, REG_RX_DATA_INFO, buf, len);
	mutex_unlock(&chip->data_lock);
	return ret;
}

static u32 nu1652_uuid_to_u32(const u8 uuid[4])
{
	return ((u32)uuid[0] << 24) | ((u32)uuid[1] << 16) |
	       ((u32)uuid[2] << 8) | uuid[3];
}

static int nu1652_refresh_auth(struct nu1652_chip *chip)
{
	u8 buf[RX_RUNTIME_RX_DATA_LEN] = { 0 };
	int ret;

	ret = nu1652_get_runtime(chip, buf, sizeof(buf));
	if (ret)
		return ret;

	chip->power_mode = buf[RX_RUNTIME_POWER_MODE];
	chip->max_power = buf[RX_RUNTIME_MAX_POWER];
	chip->adapter_type = buf[RX_RUNTIME_ADAPTER_TYPE];
	chip->auth_value = buf[RX_RUNTIME_AUTH];
	chip->uuid[0] = buf[RX_RUNTIME_UUID0];
	chip->uuid[1] = buf[RX_RUNTIME_UUID1];
	chip->uuid[2] = buf[RX_RUNTIME_UUID2];
	chip->uuid[3] = buf[RX_RUNTIME_UUID3];
	if (chip->adapter_type == ADAPTER_NONE && chip->auth_value)
		chip->adapter_type = ADAPTER_AUTH_FAILED;
	return 0;
}

static int nu1652_parse_pair_property(struct device_node *node,
				       const char *property,
				       struct nu1652_fod_pair *pairs,
				       u8 expected_pairs)
{
	u8 *raw;
	int len, i, ret;

	if (!property || !strcmp(property, "null"))
		return -ENOENT;
	len = of_property_count_elems_of_size(node, property, sizeof(u8));
	if (len <= 0 || len > NU1652_MAX_FOD_PAIRS * 2 || (len & 1))
		return -EINVAL;
	if (expected_pairs && len / 2 < expected_pairs)
		return -EINVAL;

	raw = kmalloc(len, GFP_KERNEL);
	if (!raw)
		return -ENOMEM;
	ret = of_property_read_u8_array(node, property, raw, len);
	if (!ret) {
		for (i = 0; i < len / 2; i++) {
			pairs[i].gain = raw[i * 2];
			pairs[i].offset = raw[i * 2 + 1];
		}
	}
	kfree(raw);
	return ret;
}

static int nu1652_parse_fod_group(struct device_node *node,
				   const char *list_property, int group,
				   struct nu1652_fod_group *fod)
{
	const char *type_s, *len_s, *uuid_s, *normal_s, *mag_s;
	int base = group * NU1652_FOD_FIELDS;
	unsigned int value;
	int ret;

	memset(fod, 0, sizeof(*fod));
	ret = of_property_read_string_index(node, list_property, base, &type_s);
	ret |= of_property_read_string_index(node, list_property, base + 1, &len_s);
	ret |= of_property_read_string_index(node, list_property, base + 2, &uuid_s);
	ret |= of_property_read_string_index(node, list_property, base + 3, &normal_s);
	ret |= of_property_read_string_index(node, list_property, base + 4, &mag_s);
	if (ret)
		return ret;

	if (kstrtouint(type_s, 10, &value) || value > U8_MAX)
		return -EINVAL;
	fod->type = value;
	if (kstrtouint(len_s, 10, &value) || !value ||
	    value > NU1652_MAX_FOD_PAIRS)
		return -EINVAL;
	fod->length = value;
	if (kstrtou32(uuid_s, 16, &fod->uuid))
		return -EINVAL;

	ret = nu1652_parse_pair_property(node, normal_s, fod->normal,
					 fod->length);
	if (ret)
		return ret;
	ret = nu1652_parse_pair_property(node, mag_s, fod->magnetic,
					 fod->length);
	if (ret == -ENOENT)
		memcpy(fod->magnetic, fod->normal, sizeof(fod->normal));
	else if (ret)
		return ret;

	fod->valid = true;
	return 0;
}

static int nu1652_parse_fod_tables(struct nu1652_chip *chip)
{
	struct device_node *node;
	int count, groups, i, ret;

	node = of_find_node_by_name(NULL, "mca_nu1652_fod_data");
	if (!node)
		return -ENOENT;

	count = of_property_count_strings(node, "fod_params");
	if (count > 0 && !(count % NU1652_FOD_FIELDS)) {
		groups = min(count / NU1652_FOD_FIELDS, NU1652_MAX_FOD_GROUPS);
		for (i = 0; i < groups; i++) {
			ret = nu1652_parse_fod_group(node, "fod_params", i,
						      &chip->fod[chip->fod_count]);
			if (!ret)
				chip->fod_count++;
		}
	}

	count = of_property_count_strings(node, "fod_params_default");
	if (count >= NU1652_FOD_FIELDS)
		(void)nu1652_parse_fod_group(node, "fod_params_default", 0,
					      &chip->fod_default);
	count = of_property_count_strings(node, "fod_params_bpp_plus");
	if (count >= NU1652_FOD_FIELDS)
		(void)nu1652_parse_fod_group(node, "fod_params_bpp_plus", 0,
					      &chip->fod_bpp_plus);

	of_node_put(node);
	mca_log_info("parsed %d NU1652 UUID FOD groups\n", chip->fod_count);
	return 0;
}

static int nu1652_set_enable_mode(bool enable, void *data)
{
	struct nu1652_chip *chip = data;

	if (!gpio_is_valid(chip->sleep_gpio))
		return -ENODEV;
	return gpio_direction_output(chip->sleep_gpio, !enable);
}

static int nu1652_is_present(int *present, void *data)
{
	struct nu1652_chip *chip = data;

	if (!present)
		return -EINVAL;
	*present = chip->present;
	return 0;
}

static int nu1652_get_measurement(struct nu1652_chip *chip, int low, int high,
				   int *value)
{
	u8 buf[30] = { 0 };
	int ret;

	if (!value)
		return -EINVAL;
	if (!chip->present) {
		*value = 0;
		return 0;
	}
	ret = nu1652_get_runtime(chip, buf, sizeof(buf));
	if (ret)
		return ret;
	*value = ((int)buf[high] << 8) | buf[low];
	return 0;
}

static int nu1652_get_vout(int *vout, void *data)
{
	return nu1652_get_measurement(data, RX_RUNTIME_VOUT_L,
				      RX_RUNTIME_VOUT_H, vout);
}

static int nu1652_get_iout(int *iout, void *data)
{
	return nu1652_get_measurement(data, RX_RUNTIME_IOUT_L,
				      RX_RUNTIME_IOUT_H, iout);
}

static int nu1652_get_vrect(int *vrect, void *data)
{
	return nu1652_get_measurement(data, RX_RUNTIME_VRECT_L,
				      RX_RUNTIME_VRECT_H, vrect);
}

static int nu1652_get_temp(int *temp, void *data)
{
	return nu1652_get_measurement(data, RX_RUNTIME_TEMP_L,
				      RX_RUNTIME_TEMP_H, temp);
}

static int nu1652_set_vout(int vout, void *data)
{
	struct nu1652_chip *chip = data;
	u8 lo, hi;
	int ret;

	if (!chip->present)
		return -ENODATA;
	vout = clamp(vout, RX_VOUT_MIN_MV, RX_VOUT_MAX_MV);
	if (!vout)
		vout = RX_VOUT_DEFAULT_MV;
	lo = vout & 0xff;
	hi = vout >> 8;

	mutex_lock(&chip->data_lock);
	ret = nu1652_wait_free(chip, REG_RX_REV_DATA5);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_CMD, RX_CMD_SET_RX_VOUT);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA1, RX_VOUT_PACKET_LENGTH);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA2, lo);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA3, hi);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_INT_0, RX_VOUT_TRIGGER);
	mutex_unlock(&chip->data_lock);
	if (!ret)
		chip->vout_setted = vout;
	return ret;
}

static int nu1652_get_vout_setted(int *vout, void *data)
{
	struct nu1652_chip *chip = data;

	if (!vout)
		return -EINVAL;
	*vout = chip->vout_setted;
	return 0;
}

static int nu1652_get_tx_adapter(int *adapter, void *data)
{
	struct nu1652_chip *chip = data;
	int ret;

	if (!adapter)
		return -EINVAL;
	if (!chip->present) {
		*adapter = ADAPTER_NONE;
		return 0;
	}
	ret = nu1652_refresh_auth(chip);
	if (!ret)
		*adapter = chip->adapter_type;
	return ret;
}

static int nu1652_get_tx_adapter_by_i2c(int *adapter, void *data)
{
	return nu1652_get_tx_adapter(adapter, data);
}

static int nu1652_get_rx_power_mode(u8 *mode, void *data)
{
	struct nu1652_chip *chip = data;
	int ret;

	if (!mode)
		return -EINVAL;
	ret = nu1652_refresh_auth(chip);
	if (!ret)
		*mode = chip->power_mode;
	return ret;
}

static int nu1652_get_tx_max_power(u8 *power, void *data)
{
	struct nu1652_chip *chip = data;
	int ret;

	if (!power)
		return -EINVAL;
	ret = nu1652_refresh_auth(chip);
	if (!ret)
		*power = chip->max_power;
	return ret;
}

static int nu1652_get_auth_value(int *value, void *data)
{
	struct nu1652_chip *chip = data;
	int ret;

	if (!value)
		return -EINVAL;
	ret = nu1652_refresh_auth(chip);
	if (!ret)
		*value = chip->auth_value;
	return ret;
}

static int nu1652_get_tx_uuid(u8 *uuid, void *data)
{
	struct nu1652_chip *chip = data;
	int ret;

	if (!uuid)
		return -EINVAL;
	ret = nu1652_refresh_auth(chip);
	if (!ret)
		memcpy(uuid, chip->uuid, sizeof(chip->uuid));
	return ret;
}

static int nu1652_get_fw_version(char *buf, void *data)
{
	struct nu1652_chip *chip = data;
	u8 runtime[3] = { 0 };
	int ret;

	if (!buf)
		return -EINVAL;
	ret = nu1652_get_runtime(chip, runtime, sizeof(runtime));
	if (ret)
		return ret;
	return scnprintf(buf, 32, "%02x.%02x.%02x",
			 runtime[0], runtime[1], runtime[2]);
}

static int nu1652_check_i2c(void *data)
{
	struct nu1652_chip *chip = data;
	u8 value = 0;
	int ret;

	mutex_lock(&chip->data_lock);
	ret = nu1652_write(chip, REG_RX_SENT_CMD, RX_CMD_START_READ);
	if (!ret) {
		msleep(20);
		ret = nu1652_read(chip, REG_RX_SENT_CMD, &value);
		if (!ret && value != RX_CMD_START_READ)
			ret = -EIO;
	}
	mutex_unlock(&chip->data_lock);
	return ret;
}

static int nu1652_set_adapter_voltage(int voltage, void *data)
{
	struct nu1652_chip *chip = data;
	u8 lo, hi;
	int ret;

	if (!chip->present)
		return -ENODATA;
	if (voltage < RX_ADAPTER_MIN_MV || voltage > RX_ADAPTER_MAX_MV)
		voltage = RX_ADAPTER_DEFAULT_MV;
	lo = voltage & 0xff;
	hi = voltage >> 8;

	mutex_lock(&chip->data_lock);
	ret = nu1652_wait_free(chip, REG_RX_REV_DATA5);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_CMD, RX_CMD_TRANSMIT_PACKET);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA1, RX_ADAPTER_PACKET_LENGTH);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA2, RX_ADAPTER_SET_TYPE);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA3, RX_TRANSPARENT_3BYTE);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA4,
				    RX_ADAPTER_SET_VOLTAGE_CMD);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA5, lo);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA6, hi);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_INT_0, RX_ADAPTER_TRIGGER);
	mutex_unlock(&chip->data_lock);
	return ret;
}

static int nu1652_send_transparent_data(u8 *send_data, u8 length, void *data)
{
	struct nu1652_chip *chip = data;
	int i, ret;

	if (!send_data || !length || length > 32)
		return -EINVAL;
	mutex_lock(&chip->data_lock);
	ret = nu1652_wait_free(chip, REG_RX_REV_DATA5);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_CMD, RX_CMD_TRANSMIT_PACKET);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA1, length);
	for (i = 0; !ret && i < length; i++)
		ret = nu1652_write(chip, REG_RX_SENT_DATA2 + i, send_data[i]);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_INT_0, length + 2);
	mutex_unlock(&chip->data_lock);
	return ret;
}

static int nu1652_receive_transparent_data(u8 *rcv, int buf_len, int *rcv_len,
					    void *data)
{
	struct nu1652_chip *chip = data;
	u8 runtime[NU1652_RUNTIME_LEN] = { 0 };
	int bytes, ret;

	if (!rcv || !rcv_len || buf_len <= 0)
		return -EINVAL;
	ret = nu1652_get_runtime(chip, runtime, sizeof(runtime));
	if (ret)
		return ret;
	bytes = (runtime[RX_RUNTIME_TRANS_LEN] >> 4) +
		(runtime[RX_RUNTIME_TRANS_LEN] & 0x0f);
	if (bytes < 0 || bytes > buf_len ||
	    RX_RUNTIME_TRANS_DATA + bytes > sizeof(runtime))
		return -EMSGSIZE;
	memcpy(rcv, &runtime[RX_RUNTIME_TRANS_DATA], bytes);
	*rcv_len = bytes;
	return 0;
}

static int nu1652_do_renego(u8 max_power, void *data)
{
	struct nu1652_chip *chip = data;
	int ret;

	mutex_lock(&chip->data_lock);
	ret = nu1652_wait_free(chip, REG_RX_REV_DATA5);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_CMD, RX_CMD_RENEGO_SET);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA1, RX_RENEGO_LENGTH);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA2, max_power);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_INT_0, RX_RENEGO_TRIGGER);
	mutex_unlock(&chip->data_lock);
	return ret;
}

static int nu1652_set_parallel_charge(bool parallel, void *data)
{
	return 0;
}

static int nu1652_enable_reverse_chg(bool enable, void *data)
{
	struct nu1652_chip *chip = data;

	return nu1652_write(chip, REG_RX_INT_3,
			     enable ? RX_CMD_ENABLE_TX : RX_CMD_DISABLE_TX);
}

static int nu1652_enable_reverse_fod(bool enable, void *data)
{
	struct nu1652_chip *chip = data;
	int ret;

	mutex_lock(&chip->data_lock);
	ret = nu1652_wait_free(chip, REG_RX_REV_DATA5);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_CMD,
				    RX_CMD_ENABLE_REVERSE_FOD);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA1,
				    enable ? RX_REVERSE_FOD_ENABLE :
					     RX_REVERSE_FOD_DISABLE);
	if (!ret && enable)
		ret = nu1652_write(chip, REG_RX_SENT_DATA2, RX_REVERSE_FOD_GAIN);
	if (!ret && enable)
		ret = nu1652_write(chip, REG_RX_SENT_DATA3, 0);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_INT_0,
				    enable ? RX_REVERSE_FOD_TRIGGER :
					     RX_REVERSE_FOD_DISABLE_TRIGGER);
	mutex_unlock(&chip->data_lock);
	return ret;
}

static int nu1652_get_rx_rtx_mode(int *mode, void *data)
{
	struct nu1652_chip *chip = data;
	u8 value;
	int ret;

	if (!mode)
		return -EINVAL;
	ret = nu1652_read(chip, REG_RX_RTX_MODE, &value);
	if (!ret)
		*mode = value == RX_MODE_STATUS_RTX;
	return ret;
}

static int nu1652_get_rx_fastcharge_status(u8 *flag, void *data)
{
	struct nu1652_chip *chip = data;
	int ret;

	if (!flag)
		return -EINVAL;
	mutex_lock(&chip->data_lock);
	ret = nu1652_prepare_runtime_read(chip);
	if (!ret)
		ret = nu1652_read(chip, REG_RX_FASTCHG_RESULT, flag);
	mutex_unlock(&chip->data_lock);
	return ret;
}

static int nu1652_get_ss_voltage(int *voltage, void *data)
{
	struct nu1652_chip *chip = data;
	u8 buf[2];
	int ret;

	if (!voltage)
		return -EINVAL;
	if (!chip->present) {
		*voltage = 0;
		return 0;
	}
	mutex_lock(&chip->data_lock);
	ret = nu1652_prepare_runtime_read(chip);
	if (!ret)
		ret = nu1652_read_buffer(chip, REG_RX_SS_VOLTAGE, buf, sizeof(buf));
	mutex_unlock(&chip->data_lock);
	if (!ret)
		*voltage = ((int)buf[1] << 8) | buf[0];
	return ret;
}

static int nu1652_set_rx_offset(int offset, void *data)
{
	struct nu1652_chip *chip = data;

	chip->fake_rx_offset = offset;
	return 0;
}

static int nu1652_get_rx_offset(int *offset, void *data)
{
	struct nu1652_chip *chip = data;
	int ss = 0, ret;

	if (!offset)
		return -EINVAL;
	*offset = chip->fake_rx_offset ? 1 : 0;
	if (!chip->present)
		return 0;
	ret = nu1652_get_ss_voltage(&ss, chip);
	if (!ret && ss > 0 && ss < RX_OFFSET_THRESHOLD_MV)
		*offset = 1;
	return ret;
}

static int nu1652_set_rx_sleep_mode(int sleep_for_dam, void *data)
{
	struct nu1652_chip *chip = data;

	if (!sleep_for_dam)
		return 0;
	return nu1652_write(chip, REG_RX_INT_3, RX_MODE_STATUS_RTX);
}

static int nu1652_get_poweroff_err_code(u8 *code, void *data)
{
	struct nu1652_chip *chip = data;

	if (!code)
		return -EINVAL;
	return nu1652_read(chip, REG_RX_POWER_OFF_ERR, code);
}

static int nu1652_get_project_vendor(int *vendor, void *data)
{
	struct nu1652_chip *chip = data;

	if (!vendor)
		return -EINVAL;
	*vendor = chip->project_vendor;
	return 0;
}

static int nu1652_get_hall_status(bool *status, void *data)
{
	struct nu1652_chip *chip = data;

	if (!status)
		return -EINVAL;
	*status = chip->support_hall && gpio_is_valid(chip->hall_gpio) ?
		 !!gpio_get_value(chip->hall_gpio) : false;
	return 0;
}

static int nu1652_get_magnetic_case(bool *status, void *data)
{
	struct nu1652_chip *chip = data;

	if (!status)
		return -EINVAL;
	*status = chip->magnetic_case;
	return 0;
}

static int nu1652_get_trx_isense(int *isense, void *data)
{
	return nu1652_get_measurement(data, RX_RUNTIME_TRX_ISENSE_L,
				      RX_RUNTIME_TRX_ISENSE_H, isense);
}

static int nu1652_get_trx_vrect(int *vrect, void *data)
{
	return nu1652_get_measurement(data, RX_RUNTIME_TRX_VRECT_L,
				      RX_RUNTIME_TRX_VRECT_H, vrect);
}

static int nu1652_write_fod_group(struct nu1652_chip *chip,
				   const struct nu1652_fod_group *fod,
				   bool bpp_plus)
{
	const struct nu1652_fod_pair *pairs;
	u8 raw[RX_FOD_CHUNK_PAIRS * 2];
	int offset = 0, count, bytes, ret = 0;

	if (!fod || !fod->valid || !fod->length)
		return -ENODATA;
	pairs = chip->magnetic_case ? fod->magnetic : fod->normal;

	mutex_lock(&chip->data_lock);
	ret = nu1652_wait_free(chip, REG_RX_REV_DATA5);
	if (ret)
		goto out;

	if (bpp_plus) {
		count = min_t(int, RX_FOD_CHUNK_PAIRS, fod->length);
		bytes = count * sizeof(*pairs);
		memcpy(raw, pairs, bytes);
		ret = nu1652_write(chip, REG_RX_SENT_CMD, RX_CMD_FOD_SET);
		if (!ret)
			ret = nu1652_write(chip, REG_RX_SENT_DATA1,
					    RX_FOD_BPP_PACKET_LENGTH);
		if (!ret)
			ret = nu1652_write(chip, REG_RX_SENT_DATA2, fod->type);
		if (!ret)
			ret = nu1652_write(chip, REG_RX_SENT_DATA3, 0);
		if (!ret)
			ret = nu1652_write(chip, REG_RX_SENT_DATA4, bytes);
		if (!ret)
			ret = nu1652_write_buffer(chip, REG_RX_SENT_DATA5, raw, bytes);
		if (!ret)
			ret = nu1652_write(chip, REG_RX_INT_0, RX_FOD_BPP_TRIGGER);
		goto out;
	}

	while (offset < fod->length) {
		count = min_t(int, RX_FOD_CHUNK_PAIRS, fod->length - offset);
		bytes = count * sizeof(*pairs);
		memcpy(raw, &pairs[offset], bytes);
		ret = nu1652_write(chip, REG_RX_SENT_CMD, RX_CMD_FOD_SET);
		if (!ret)
			ret = nu1652_write(chip, REG_RX_SENT_DATA1, bytes + 3);
		if (!ret)
			ret = nu1652_write(chip, REG_RX_SENT_DATA2, fod->type);
		if (!ret)
			ret = nu1652_write(chip, REG_RX_SENT_DATA3, offset);
		if (!ret)
			ret = nu1652_write(chip, REG_RX_SENT_DATA4, bytes);
		if (!ret)
			ret = nu1652_write_buffer(chip, REG_RX_SENT_DATA5, raw, bytes);
		if (!ret)
			ret = nu1652_write(chip, REG_RX_INT_0, bytes + 5);
		if (ret)
			break;
		msleep(20);
		offset += count;
	}
out:
	mutex_unlock(&chip->data_lock);
	return ret;
}

static int nu1652_set_fod_params(int value, void *data)
{
	struct nu1652_chip *chip = data;
	u32 uuid;
	int i, ret;

	ret = nu1652_refresh_auth(chip);
	if (ret)
		return ret;
	uuid = nu1652_uuid_to_u32(chip->uuid);
	for (i = 0; i < chip->fod_count; i++) {
		if (chip->fod[i].valid && chip->fod[i].uuid == uuid)
			return nu1652_write_fod_group(chip, &chip->fod[i], false);
	}

	/* Stock selects BPP+ for generic QC3/PD and default for Xiaomi EPP+. */
	if ((chip->adapter_type == ADAPTER_QC3 ||
	     chip->adapter_type == ADAPTER_PD) && chip->power_mode == 0 &&
	    chip->fod_bpp_plus.valid)
		return nu1652_write_fod_group(chip, &chip->fod_bpp_plus, true);
	if (chip->adapter_type >= ADAPTER_XIAOMI_QC3 && chip->fod_default.valid)
		return nu1652_write_fod_group(chip, &chip->fod_default, false);
	return -ENODATA;
}

static int nu1652_get_last_irq(int *irq_flag, void *data)
{
	struct nu1652_chip *chip = data;

	if (!irq_flag)
		return -EINVAL;
	*irq_flag = chip->last_irq;
	return 0;
}

static int nu1652_clear_irq(struct nu1652_chip *chip, u8 lo, u8 hi)
{
	int ret;

	mutex_lock(&chip->data_lock);
	ret = nu1652_wait_free(chip, REG_RX_REV_DATA5);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_CMD, RX_CMD_CLEAR_INT);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA1, RX_CLEAR_INT_LENGTH);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA2, lo);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_SENT_DATA3, hi);
	if (!ret)
		ret = nu1652_write(chip, REG_RX_INT_0, RX_CLEAR_INT_TRIGGER);
	mutex_unlock(&chip->data_lock);
	return ret;
}

static int nu1652_map_irq(u16 flags)
{
	int bit;

	if (!flags)
		return 0;
	bit = __ffs(flags);
	return bit + 1;
}

static irqreturn_t nu1652_rx_irq_thread(int irq, void *data)
{
	struct nu1652_chip *chip = data;
	u8 lo = 0, hi = 0;
	u16 flags;
	int mode = 0;

	if (nu1652_read(chip, REG_RX_REV_CMD, &lo) ||
	    nu1652_read(chip, REG_RX_REV_DATA1, &hi))
		return IRQ_HANDLED;
	flags = ((u16)hi << 8) | lo;
	chip->last_irq = nu1652_map_irq(flags);
	(void)nu1652_get_rx_rtx_mode(&mode, chip);
	chip->last_trx_mode = mode;
	(void)nu1652_clear_irq(chip, lo, hi);
	if (mode)
		(void)mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS,
						MCA_EVENT_WIRELESS_INT_CHANGE,
						chip->last_irq);
	else
		(void)mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
						MCA_EVENT_WIRELESS_INT_CHANGE,
						chip->last_irq);
	return IRQ_HANDLED;
}

static void nu1652_update_present(struct nu1652_chip *chip)
{
	bool present = !!gpio_get_value(chip->power_good_gpio);

	if (present == chip->present)
		return;
	chip->present = present;
	if (!present) {
		chip->adapter_type = ADAPTER_NONE;
		chip->power_mode = 0;
		chip->max_power = 0;
		chip->auth_value = 0;
		memset(chip->uuid, 0, sizeof(chip->uuid));
	}
	mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT,
		present ? MCA_EVENT_WIRELESS_CONNECT : MCA_EVENT_WIRELESS_DISCONNECT,
		NULL);
}

static irqreturn_t nu1652_pg_irq_thread(int irq, void *data)
{
	struct nu1652_chip *chip = data;

	nu1652_update_present(chip);
	return IRQ_HANDLED;
}

static irqreturn_t nu1652_hall_irq_thread(int irq, void *data)
{
	struct nu1652_chip *chip = data;

	chip->magnetic_case = !gpio_get_value(chip->hall_gpio);
	return IRQ_HANDLED;
}

static struct platform_class_wireless_ops nu1652_wls_ops = {
	.wls_enable_reverse_chg = nu1652_enable_reverse_chg,
	.wls_is_present = nu1652_is_present,
	.wls_set_vout = nu1652_set_vout,
	.wls_get_vout = nu1652_get_vout,
	.wls_get_iout = nu1652_get_iout,
	.wls_get_vrect = nu1652_get_vrect,
	.wls_get_tx_adapter = nu1652_get_tx_adapter,
	.wls_get_tx_adapter_by_i2c = nu1652_get_tx_adapter_by_i2c,
	.wls_get_temp = nu1652_get_temp,
	.wls_set_enable_mode = nu1652_set_enable_mode,
	.wls_get_fw_version = nu1652_get_fw_version,
	.wls_get_rx_rtx_mode = nu1652_get_rx_rtx_mode,
	.wls_get_rx_int_flag = nu1652_get_last_irq,
	.wls_get_rx_power_mode = nu1652_get_rx_power_mode,
	.wls_get_tx_max_power = nu1652_get_tx_max_power,
	.wls_get_auth_value = nu1652_get_auth_value,
	.wls_set_adapter_voltage = nu1652_set_adapter_voltage,
	.wls_get_tx_uuid = nu1652_get_tx_uuid,
	.wls_set_fod_params = nu1652_set_fod_params,
	.wls_get_rx_fastcharge_status = nu1652_get_rx_fastcharge_status,
	.wls_receive_transparent_data = nu1652_receive_transparent_data,
	.wls_send_transparent_data = nu1652_send_transparent_data,
	.wls_get_ss_voltage = nu1652_get_ss_voltage,
	.wls_do_renego = nu1652_do_renego,
	.wls_set_parallel_charge = nu1652_set_parallel_charge,
	.wls_get_vout_setted = nu1652_get_vout_setted,
	.wls_get_poweroff_err_code = nu1652_get_poweroff_err_code,
	.wls_get_project_vendor = nu1652_get_project_vendor,
	.wls_check_i2c_is_ok = nu1652_check_i2c,
	.wls_enable_rev_fod = nu1652_enable_reverse_fod,
	.wls_get_hall_gpio_status = nu1652_get_hall_status,
	.wls_get_magnetic_case_flag = nu1652_get_magnetic_case,
	.wls_set_rx_offset = nu1652_set_rx_offset,
	.wls_get_rx_offset = nu1652_get_rx_offset,
	.wls_set_rx_sleep_mode = nu1652_set_rx_sleep_mode,
	.wls_get_trx_isense = nu1652_get_trx_isense,
	.wls_get_trx_vrect = nu1652_get_trx_vrect,
};

static int nu1652_request_gpio(struct nu1652_chip *chip, const char *name,
				int *gpio, unsigned long flags)
{
	int ret;

	*gpio = of_get_named_gpio(chip->dev->of_node, name, 0);
	if (!gpio_is_valid(*gpio))
		return *gpio < 0 ? *gpio : -EINVAL;
	ret = devm_gpio_request_one(chip->dev, *gpio, flags, name);
	return ret;
}

static int nu1652_probe(struct i2c_client *client)
{
	struct nu1652_chip *chip;
	int ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	chip->dev = &client->dev;
	chip->client = client;
	chip->regmap = devm_regmap_init_i2c(client, &nu1652_regmap_config);
	if (IS_ERR(chip->regmap))
		return PTR_ERR(chip->regmap);
	mutex_init(&chip->io_lock);
	mutex_init(&chip->data_lock);
	i2c_set_clientdata(client, chip);

	(void)of_property_read_u32(client->dev.of_node, "rx_role", &chip->role);
	(void)of_property_read_u32(client->dev.of_node, "project_vendor",
				   &chip->project_vendor);
	chip->support_hall = of_property_read_bool(client->dev.of_node,
						   "support-hall") ||
			     of_property_read_bool(client->dev.of_node,
						   "support_hall");
	/* Some stock DTs encode support-hall as a u32 rather than a boolean. */
	if (!chip->support_hall) {
		u32 hall = 0;
		if (!of_property_read_u32(client->dev.of_node, "support-hall", &hall))
			chip->support_hall = !!hall;
	}
	if (chip->role >= WIRELESS_ROLE_MAX)
		return -EINVAL;

	ret = nu1652_request_gpio(chip, "sleep-rx-gpio", &chip->sleep_gpio,
				   GPIOF_OUT_INIT_LOW);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "missing sleep-rx-gpio\n");
	ret = nu1652_request_gpio(chip, "pwr-det-int", &chip->power_good_gpio,
				   GPIOF_IN);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "missing pwr-det-int\n");
	ret = nu1652_request_gpio(chip, "rx-int", &chip->rx_irq_gpio, GPIOF_IN);
	if (ret)
		return dev_err_probe(&client->dev, ret, "missing rx-int\n");

	if (chip->support_hall) {
		ret = nu1652_request_gpio(chip, "hall-int2", &chip->hall_gpio,
					   GPIOF_IN);
		if (ret)
			return dev_err_probe(&client->dev, ret,
					     "missing hall-int2\n");
		chip->magnetic_case = !gpio_get_value(chip->hall_gpio);
	}

	chip->present = !!gpio_get_value(chip->power_good_gpio);
	chip->vout_setted = RX_VOUT_DEFAULT_MV;
	(void)nu1652_parse_fod_tables(chip);

	chip->power_good_irq = gpio_to_irq(chip->power_good_gpio);
	chip->rx_irq = gpio_to_irq(chip->rx_irq_gpio);
	if (chip->power_good_irq < 0 || chip->rx_irq < 0)
		return -EINVAL;

	ret = devm_request_threaded_irq(&client->dev, chip->power_good_irq,
					NULL, nu1652_pg_irq_thread,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
					IRQF_ONESHOT,
					"nu1652-power-good", chip);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to request power-good IRQ\n");
	ret = devm_request_threaded_irq(&client->dev, chip->rx_irq, NULL,
					nu1652_rx_irq_thread,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"nu1652-rx", chip);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to request RX IRQ\n");

	if (chip->support_hall) {
		chip->hall_irq = gpio_to_irq(chip->hall_gpio);
		if (chip->hall_irq < 0)
			return chip->hall_irq;
		ret = devm_request_threaded_irq(&client->dev, chip->hall_irq, NULL,
						nu1652_hall_irq_thread,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"nu1652-hall", chip);
		if (ret)
			return dev_err_probe(&client->dev, ret,
					     "failed to request hall IRQ\n");
	}

	ret = platform_class_wireless_register_ops(chip->role, chip,
						   &nu1652_wls_ops);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to register wireless ops\n");

	mca_log_info("runtime driver ready role=%u vendor=%u present=%d fod=%d\n",
		     chip->role, chip->project_vendor, chip->present,
		     chip->fod_count);
	return 0;
}

static void nu1652_remove(struct i2c_client *client)
{
	struct nu1652_chip *chip = i2c_get_clientdata(client);

	if (!chip)
		return;
	mutex_destroy(&chip->data_lock);
	mutex_destroy(&chip->io_lock);
}

static const struct of_device_id nu1652_of_match[] = {
	{ .compatible = "fuda,nu1652" },
	{},
};
MODULE_DEVICE_TABLE(of, nu1652_of_match);

static const struct i2c_device_id nu1652_id[] = {
	{ "nu1652", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, nu1652_id);

static struct i2c_driver nu1652_driver = {
	.driver = {
		.name = "mca_nu1652",
		.of_match_table = nu1652_of_match,
	},
	.probe = nu1652_probe,
	.remove = nu1652_remove,
	.id_table = nu1652_id,
};
module_i2c_driver(nu1652_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA NU1652 wireless runtime driver");
MODULE_LICENSE("GPL v2");
