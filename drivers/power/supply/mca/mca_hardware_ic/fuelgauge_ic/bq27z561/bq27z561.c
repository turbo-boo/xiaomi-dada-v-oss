// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi MCA BQ27Z561 fuel-gauge driver for Dada.
 *
 * Keep destructive/OTA policy out of hardware bring-up, while preserving the
 * stock MCA measurement and userspace identity ABI used by Xiaomi services.
 */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_fg_ic_ops.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "bq27z561"
#endif

#define BQ27Z561_REG_TEMP		0x06
#define BQ27Z561_REG_VOLT		0x08
#define BQ27Z561_REG_FLAGS		0x0a
#define BQ27Z561_REG_CURRENT		0x0c
#define BQ27Z561_REG_RM			0x10
#define BQ27Z561_REG_FCC		0x12
#define BQ27Z561_REG_AVG_CURRENT	0x14
#define BQ27Z561_REG_TTE		0x16
#define BQ27Z561_REG_TTF		0x18
#define BQ27Z561_REG_INT_TEMP		0x1e
#define BQ27Z561_REG_CYCLE		0x2a
#define BQ27Z561_REG_SOC		0x2c
#define BQ27Z561_REG_SOH		0x2e
#define BQ27Z561_REG_CHG_VOLT		0x30
#define BQ27Z561_REG_CHG_CURR		0x32
#define BQ27Z561_REG_DESIGN_CAP		0x3c
#define BQ27Z561_REG_ALT_MAC		0x3e
#define BQ27Z561_MAC_FRAME_LEN		36
#define BQ27Z561_MAC_CHECKSUM_POS	34
#define BQ27Z561_MAC_LENGTH_POS		35

#define BQ27Z561_MAC_DEVICE_NAME	0x004a
#define BQ27Z561_MAC_CHEM_NAME		0x004b
#define BQ27Z561_MAC_MANU_NAME		0x004c

#define BQ27Z561_FLAG_FC		BIT(5)
#define BQ27Z561_FLAG_DSG		BIT(6)
#define BQ27Z561_FAKE_TEMP_NONE	(-999)

/* Numeric ABI from Xiaomi's public BQ27Z561 header. */
enum bq27z561_pack_vendor {
	PACK_SUPPLIER_BYD = 0,
	PACK_SUPPLIER_COSLIGHT,
	PACK_SUPPLIER_SUNWODA,
	PACK_SUPPLIER_NVT,
	PACK_SUPPLIER_SCUD,
	PACK_SUPPLIER_TWS,
	PACK_SUPPLIER_LISHEN,
	PACK_SUPPLIER_DESAY,
};

enum bq27z561_sysfs_attr {
	BQ_SYSFS_CHIP_OK = 0,
	BQ_SYSFS_VBATT,
	BQ_SYSFS_IBATT,
	BQ_SYSFS_RSOC,
	BQ_SYSFS_TEMP,
	BQ_SYSFS_CYCLECOUNT,
	BQ_SYSFS_RM,
	BQ_SYSFS_FCC,
	BQ_SYSFS_SOH,
	BQ_SYSFS_PACK_VENDOR,
	BQ_SYSFS_DESIGN_CAPACITY,
	BQ_SYSFS_DEVICE_NAME,
};

struct bq27z561_mca {
	struct device *dev;
	struct i2c_client *client;
	struct device *sysfs_dev;
	struct mutex io_lock;
	unsigned int role;
	int fake_temp;
	int fake_cycle;
	int pack_vendor;
	int fg_vendor;
	char device_name[6];
	char fake_device_name[16];
	bool online;
};

static int bq27z561_read_word(struct bq27z561_mca *bq, u8 reg, u16 *value)
{
	struct i2c_msg msgs[2];
	u8 addr = reg;
	u8 data[2] = { 0 };
	int ret;

	if (!bq || !bq->client || !bq->client->adapter || !value)
		return -EINVAL;

	msgs[0].addr = bq->client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &addr;
	msgs[1].addr = bq->client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = sizeof(data);
	msgs[1].buf = data;

	mutex_lock(&bq->io_lock);
	ret = i2c_transfer(bq->client->adapter, msgs, ARRAY_SIZE(msgs));
	mutex_unlock(&bq->io_lock);
	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*value = data[0] | ((u16)data[1] << 8);
	return 0;
}

static u8 bq27z561_checksum(const u8 *data, u8 len)
{
	u16 sum = 0;
	u8 i;

	for (i = 0; i < len; i++)
		sum += data[i];
	return 0xff - (sum & 0xff);
}

/*
 * Xiaomi/TI AltManufacturerAccess transaction used by the stock Dada gauge:
 * write the little-endian MAC command at 0x3e, then read the 36-byte frame
 * back from 0x3e. Byte 34 is checksum and byte 35 is frame length.
 */
static int bq27z561_mac_read(struct bq27z561_mca *bq, u16 cmd,
			     u8 *out, size_t out_len)
{
	struct i2c_msg write_msg;
	struct i2c_msg read_msgs[2];
	u8 write_buf[3] = {
		BQ27Z561_REG_ALT_MAC,
		(u8)cmd,
		(u8)(cmd >> 8),
	};
	u8 addr = BQ27Z561_REG_ALT_MAC;
	u8 frame[BQ27Z561_MAC_FRAME_LEN] = { 0 };
	u8 frame_len;
	int ret;

	if (!bq || !bq->client || !bq->client->adapter || !out ||
	    out_len > BQ27Z561_MAC_CHECKSUM_POS - 2)
		return -EINVAL;

	write_msg.addr = bq->client->addr;
	write_msg.flags = 0;
	write_msg.len = sizeof(write_buf);
	write_msg.buf = write_buf;

	read_msgs[0].addr = bq->client->addr;
	read_msgs[0].flags = 0;
	read_msgs[0].len = 1;
	read_msgs[0].buf = &addr;
	read_msgs[1].addr = bq->client->addr;
	read_msgs[1].flags = I2C_M_RD;
	read_msgs[1].len = sizeof(frame);
	read_msgs[1].buf = frame;

	mutex_lock(&bq->io_lock);
	ret = i2c_transfer(bq->client->adapter, &write_msg, 1);
	if (ret == 1) {
		usleep_range(4000, 4100);
		ret = i2c_transfer(bq->client->adapter, read_msgs,
				   ARRAY_SIZE(read_msgs));
	}
	mutex_unlock(&bq->io_lock);

	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(read_msgs))
		return -EIO;
	if (frame[0] != (u8)cmd || frame[1] != (u8)(cmd >> 8))
		return -EPROTO;

	frame_len = frame[BQ27Z561_MAC_LENGTH_POS];
	if (frame_len < 4 || frame_len > BQ27Z561_MAC_FRAME_LEN)
		return -EPROTO;
	if (bq27z561_checksum(frame, frame_len - 2) !=
	    frame[BQ27Z561_MAC_CHECKSUM_POS])
		return -EIO;
	if (out_len > frame_len - 4)
		return -EMSGSIZE;

	memcpy(out, frame + 2, out_len);
	return 0;
}

static int bq27z561_read_identity(struct bq27z561_mca *bq)
{
	u8 chem[4] = { 0 };
	u8 name[8] = { 0 };
	u8 manu[32] = { 0 };
	int ret;

	ret = bq27z561_mac_read(bq, BQ27Z561_MAC_CHEM_NAME,
				chem, sizeof(chem));
	if (ret)
		return ret;

	switch (chem[2]) {
	case 'B': bq->pack_vendor = PACK_SUPPLIER_BYD; break;
	case 'C': bq->pack_vendor = PACK_SUPPLIER_COSLIGHT; break;
	case 'S': bq->pack_vendor = PACK_SUPPLIER_SUNWODA; break;
	case 'N': bq->pack_vendor = PACK_SUPPLIER_NVT; break;
	case 'U': bq->pack_vendor = PACK_SUPPLIER_SCUD; break;
	case 'T': bq->pack_vendor = PACK_SUPPLIER_TWS; break;
	case 'I': bq->pack_vendor = PACK_SUPPLIER_LISHEN; break;
	case 'K': bq->pack_vendor = PACK_SUPPLIER_DESAY; break;
	default: bq->pack_vendor = PACK_SUPPLIER_NVT; break;
	}

	ret = bq27z561_mac_read(bq, BQ27Z561_MAC_DEVICE_NAME,
				name, sizeof(name));
	if (!ret) {
		memcpy(bq->device_name, name, 5);
		bq->device_name[5] = '\0';
	}

	/* Preserve the public gauge-vendor classification used by stock MCA. */
	if (!bq27z561_mac_read(bq, BQ27Z561_MAC_MANU_NAME,
				       manu, sizeof(manu))) {
		switch (manu[2]) {
		case '4': bq->fg_vendor = 1; break;
		case '5': bq->fg_vendor = 2; break;
		case '6': bq->fg_vendor = 3; break;
		case '7': bq->fg_vendor = 4; break;
		case 'C': bq->fg_vendor = 5; break; /* BQ27Z561 */
		case '8': bq->fg_vendor = 6; break;
		default: bq->fg_vendor = 3; break;
		}
	}

	return ret;
}

static int bq27z561_read_voltage(struct bq27z561_mca *bq, int *value)
{
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_VOLT, &raw);

	if (!ret)
		*value = raw;
	return ret;
}

static int bq27z561_read_current(struct bq27z561_mca *bq, u8 reg, int *value)
{
	u16 raw;
	int signed_current;
	int ret = bq27z561_read_word(bq, reg, &raw);

	if (ret)
		return ret;
	if (raw > 32768)
		signed_current = -((int)raw - 65536);
	else
		signed_current = -(int)raw;
	*value = signed_current * 1000;
	return 0;
}

static int bq27z561_read_temp_reg(struct bq27z561_mca *bq, u8 reg, int *value)
{
	u16 raw;
	int ret = bq27z561_read_word(bq, reg, &raw);

	if (ret)
		return ret;
	*value = (int)raw - 2730;
	return 0;
}

static int bq_fg_probe_ok(void *data, bool *ok)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret;

	if (!ok)
		return -EINVAL;
	ret = bq27z561_read_word(bq, BQ27Z561_REG_VOLT, &raw);
	*ok = !ret && raw != 0 && raw != 0xffff;
	return ret;
}

static int bq_fg_get_rsoc(void *data, int *value)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_SOC, &raw);

	if (!ret)
		*value = raw;
	return ret;
}

static int bq_fg_get_curr(void *data, int *value)
{
	return bq27z561_read_current(data, BQ27Z561_REG_CURRENT, value);
}

static int bq_fg_get_volt(void *data, int *value)
{
	return bq27z561_read_voltage(data, value);
}

static int bq_fg_set_temp(void *data, int value)
{
	struct bq27z561_mca *bq = data;

	bq->fake_temp = value;
	return 0;
}

static int bq_fg_get_temp(void *data, int *value)
{
	struct bq27z561_mca *bq = data;

	if (bq->fake_temp != BQ27Z561_FAKE_TEMP_NONE) {
		*value = bq->fake_temp;
		return 0;
	}
	return bq27z561_read_temp_reg(bq, BQ27Z561_REG_TEMP, value);
}

static int bq_fg_get_original_temp(void *data, int *value)
{
	return bq27z561_read_temp_reg(data, BQ27Z561_REG_INT_TEMP, value);
}

static int bq_fg_get_rm(void *data, int *value)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_RM, &raw);

	if (!ret)
		*value = (int)raw * 1000;
	return ret;
}

static int bq_fg_get_fcc(void *data, int *value)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_FCC, &raw);

	if (!ret)
		*value = (int)raw * 1000;
	return ret;
}

static int bq_fg_get_design_capacity(void *data, int *value)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_DESIGN_CAP, &raw);

	if (!ret)
		*value = (int)raw * 1000;
	return ret;
}

static int bq_fg_get_cycle(void *data, int *value)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret;

	if (bq->fake_cycle) {
		*value = bq->fake_cycle;
		return 0;
	}
	ret = bq27z561_read_word(bq, BQ27Z561_REG_CYCLE, &raw);
	if (!ret)
		*value = raw;
	return ret;
}

static int bq_fg_get_soh(void *data, int *value)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_SOH, &raw);

	if (!ret)
		*value = raw;
	return ret;
}

static int bq_fg_get_tte(void *data, int *value)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_TTE, &raw);

	if (!ret)
		*value = raw;
	return ret;
}

static int bq_fg_get_ttf(void *data, int *value)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_TTF, &raw);

	if (!ret)
		*value = raw;
	return ret;
}

static int bq_fg_get_chg_volt(void *data, int *value)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_CHG_VOLT, &raw);

	if (!ret)
		*value = raw;
	return ret;
}

static int bq_fg_get_chip_ok(void *data, int *value)
{
	bool ok = false;
	int ret = bq_fg_probe_ok(data, &ok);

	if (value)
		*value = ok;
	return ret;
}

static int bq_fg_get_average_current(void *data, int *value)
{
	return bq27z561_read_current(data, BQ27Z561_REG_AVG_CURRENT, value);
}

static int bq_fg_get_charge_status(void *data)
{
	struct bq27z561_mca *bq = data;
	u16 flags;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_FLAGS, &flags);

	if (ret)
		return ret;
	mca_log_debug("flags=0x%04x fc=%d dsg=%d\n", flags,
		      !!(flags & BQ27Z561_FLAG_FC),
		      !!(flags & BQ27Z561_FLAG_DSG));
	return 0;
}

static struct fuelguage_ic_ops bq27z561_mca_ops = {
	.fg_ic_probe_ok = bq_fg_probe_ok,
	.fg_ic_get_rsoc = bq_fg_get_rsoc,
	.fg_ic_get_curr = bq_fg_get_curr,
	.fg_ic_get_volt = bq_fg_get_volt,
	.fg_ic_set_temp = bq_fg_set_temp,
	.fg_ic_get_temp = bq_fg_get_temp,
	.fg_ic_get_original_temp = bq_fg_get_original_temp,
	.fg_ic_get_charge_status = bq_fg_get_charge_status,
	.fg_ic_get_rm = bq_fg_get_rm,
	.fg_ic_get_chg_vol = bq_fg_get_chg_volt,
	.fg_ic_get_chip_ok = bq_fg_get_chip_ok,
	.fg_ic_get_cyclecount = bq_fg_get_cycle,
	.fg_ic_get_tte = bq_fg_get_tte,
	.fg_ic_get_ttf = bq_fg_get_ttf,
	.fg_ic_get_fcc = bq_fg_get_fcc,
	.fg_ic_get_full_design = bq_fg_get_design_capacity,
	.fg_ic_get_soh = bq_fg_get_soh,
	.fg_ic_get_average_current = bq_fg_get_average_current,
};

static ssize_t bq27z561_sysfs_show(struct device *dev,
				   struct device_attribute *attr, char *buf);
static ssize_t bq27z561_sysfs_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count);

static struct mca_sysfs_attr_info bq27z561_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(bq27z561_sysfs, 0440, BQ_SYSFS_CHIP_OK, chip_ok),
	mca_sysfs_attr_ro(bq27z561_sysfs, 0440, BQ_SYSFS_VBATT, vbatt),
	mca_sysfs_attr_ro(bq27z561_sysfs, 0440, BQ_SYSFS_IBATT, ibatt),
	mca_sysfs_attr_ro(bq27z561_sysfs, 0440, BQ_SYSFS_RSOC, rsoc),
	mca_sysfs_attr_ro(bq27z561_sysfs, 0440, BQ_SYSFS_TEMP, temp),
	mca_sysfs_attr_rw(bq27z561_sysfs, 0664, BQ_SYSFS_CYCLECOUNT, cyclecount),
	mca_sysfs_attr_ro(bq27z561_sysfs, 0440, BQ_SYSFS_RM, rm),
	mca_sysfs_attr_ro(bq27z561_sysfs, 0440, BQ_SYSFS_FCC, charger_full),
	mca_sysfs_attr_ro(bq27z561_sysfs, 0440, BQ_SYSFS_SOH, soh),
	mca_sysfs_attr_ro(bq27z561_sysfs, 0440, BQ_SYSFS_PACK_VENDOR, pack_vendor),
	mca_sysfs_attr_ro(bq27z561_sysfs, 0440, BQ_SYSFS_DESIGN_CAPACITY, design_capacity),
	mca_sysfs_attr_rw(bq27z561_sysfs, 0664, BQ_SYSFS_DEVICE_NAME, device_name),
};

#define BQ27Z561_SYSFS_ATTRS_SIZE ARRAY_SIZE(bq27z561_sysfs_field_tbl)
static struct attribute *bq27z561_sysfs_attrs[BQ27Z561_SYSFS_ATTRS_SIZE + 1];
static const struct attribute_group bq27z561_sysfs_attr_group = {
	.attrs = bq27z561_sysfs_attrs,
};

static ssize_t bq27z561_sysfs_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	struct bq27z561_mca *bq = dev_get_drvdata(dev);
	bool ok = false;
	int value = 0, ret = 0;

	if (!bq)
		return -ENODEV;
	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  bq27z561_sysfs_field_tbl,
					  BQ27Z561_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case BQ_SYSFS_CHIP_OK:
		ret = bq_fg_probe_ok(bq, &ok);
		value = ok;
		break;
	case BQ_SYSFS_VBATT:
		ret = bq_fg_get_volt(bq, &value);
		break;
	case BQ_SYSFS_IBATT:
		ret = bq_fg_get_curr(bq, &value);
		break;
	case BQ_SYSFS_RSOC:
		ret = bq_fg_get_rsoc(bq, &value);
		break;
	case BQ_SYSFS_TEMP:
		ret = bq_fg_get_temp(bq, &value);
		break;
	case BQ_SYSFS_CYCLECOUNT:
		ret = bq_fg_get_cycle(bq, &value);
		break;
	case BQ_SYSFS_RM:
		ret = bq_fg_get_rm(bq, &value);
		break;
	case BQ_SYSFS_FCC:
		ret = bq_fg_get_fcc(bq, &value);
		break;
	case BQ_SYSFS_SOH:
		ret = bq_fg_get_soh(bq, &value);
		break;
	case BQ_SYSFS_PACK_VENDOR:
		value = bq->pack_vendor;
		break;
	case BQ_SYSFS_DESIGN_CAPACITY:
		ret = bq_fg_get_design_capacity(bq, &value);
		break;
	case BQ_SYSFS_DEVICE_NAME:
		return scnprintf(buf, PAGE_SIZE, "%s",
				 bq->fake_device_name[0] ? bq->fake_device_name :
							 bq->device_name);
	default:
		return -EINVAL;
	}
	if (ret)
		return ret;
	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t bq27z561_sysfs_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	struct bq27z561_mca *bq = dev_get_drvdata(dev);
	int value;

	if (!bq)
		return -ENODEV;
	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  bq27z561_sysfs_field_tbl,
					  BQ27Z561_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case BQ_SYSFS_CYCLECOUNT:
		if (kstrtoint(buf, 10, &value))
			return -EINVAL;
		bq->fake_cycle = value;
		break;
	case BQ_SYSFS_DEVICE_NAME:
		if (count >= sizeof(bq->fake_device_name))
			return -E2BIG;
		memcpy(bq->fake_device_name, buf, count);
		bq->fake_device_name[count] = '\0';
		strim(bq->fake_device_name);
		break;
	default:
		return -EACCES;
	}
	return count;
}

static int bq27z561_sysfs_create(struct bq27z561_mca *bq)
{
	const char *name = bq->role == FG_IC_MASTER ? "fg_master" : "fg_slave";

	mca_sysfs_init_attrs(bq27z561_sysfs_attrs, bq27z561_sysfs_field_tbl,
			     BQ27Z561_SYSFS_ATTRS_SIZE);
	bq->sysfs_dev = mca_sysfs_create_group("xm_power", name,
					       &bq27z561_sysfs_attr_group);
	if (!bq->sysfs_dev)
		return -ENOMEM;
	dev_set_drvdata(bq->sysfs_dev, bq);
	return 0;
}

static int bq27z561_mca_probe(struct i2c_client *client)
{
	struct bq27z561_mca *bq;
	u32 role = FG_IC_MASTER;
	bool ok = false;
	int ret, identity_ret;

	bq = devm_kzalloc(&client->dev, sizeof(*bq), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;

	bq->dev = &client->dev;
	bq->client = client;
	bq->fake_temp = BQ27Z561_FAKE_TEMP_NONE;
	bq->pack_vendor = PACK_SUPPLIER_NVT;
	mutex_init(&bq->io_lock);

	of_property_read_u32(client->dev.of_node, "ic_role", &role);
	if (role >= FG_IC_MAX)
		return dev_err_probe(&client->dev, -EINVAL,
				     "invalid MCA fuel-gauge role %u\n", role);
	bq->role = role;
	i2c_set_clientdata(client, bq);

	ret = bq_fg_probe_ok(bq, &ok);
	if (ret || !ok)
		return dev_err_probe(&client->dev, ret ?: -ENODEV,
				     "BQ27Z561 not responding\n");

	identity_ret = bq27z561_read_identity(bq);
	if (identity_ret)
		mca_log_err("identity MAC read incomplete: %d\n", identity_ret);

	ret = platform_fg_ic_ops_register(bq->role, bq, &bq27z561_mca_ops);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to register MCA FG ops\n");

	ret = bq27z561_sysfs_create(bq);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to create fg_master sysfs\n");

	bq->online = true;
	mca_log_info("registered role=%u addr=0x%02x device=%s pack_vendor=%d fg_vendor=%d\n",
		     bq->role, client->addr, bq->device_name,
		     bq->pack_vendor, bq->fg_vendor);
	return 0;
}

static void bq27z561_mca_remove(struct i2c_client *client)
{
	struct bq27z561_mca *bq = i2c_get_clientdata(client);

	if (!bq)
		return;
	if (bq->sysfs_dev)
		mca_sysfs_remove_group("xm_power", bq->sysfs_dev,
				       &bq27z561_sysfs_attr_group);
	bq->online = false;
}

static const struct of_device_id bq27z561_mca_of_match[] = {
	{ .compatible = "ti,bq27z561_master" },
	{ .compatible = "ti,bq27z561_slave" },
	{},
};
MODULE_DEVICE_TABLE(of, bq27z561_mca_of_match);

static const struct i2c_device_id bq27z561_mca_id[] = {
	{ "bq27z561_master", 0 },
	{ "bq27z561_slave", 1 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq27z561_mca_id);

static struct i2c_driver bq27z561_mca_driver = {
	.driver = {
		.name = "mca_bq27z561",
		.of_match_table = bq27z561_mca_of_match,
	},
	.probe = bq27z561_mca_probe,
	.remove = bq27z561_mca_remove,
	.id_table = bq27z561_mca_id,
};
module_i2c_driver(bq27z561_mca_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA BQ27Z561 fuel-gauge driver");
MODULE_LICENSE("GPL v2");
