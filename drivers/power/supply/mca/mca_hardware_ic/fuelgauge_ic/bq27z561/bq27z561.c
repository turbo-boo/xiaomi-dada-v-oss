// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal Xiaomi MCA BQ27Z561 fuel-gauge driver for Dada.
 *
 * The public Xiaomi driver contains device-specific OTA, smart-charge,
 * strategy, sysfs and HWID policy.  Keep those policy layers out of the
 * hardware bring-up and expose the stable BQ27Z561 measurements through
 * platform_fg_ic_ops first.
 */
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <mca/common/mca_log.h>
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

#define BQ27Z561_FLAG_FC		BIT(5)
#define BQ27Z561_FLAG_DSG		BIT(6)
#define BQ27Z561_FAKE_TEMP_NONE	(-999)

struct bq27z561_mca {
	struct device *dev;
	struct i2c_client *client;
	struct mutex io_lock;
	unsigned int role;
	int fake_temp;
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

static int bq27z561_read_voltage(struct bq27z561_mca *bq, int *value)
{
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_VOLT, &raw);

	if (!ret)
		*value = raw; /* mV, matching Xiaomi MCA ABI */
	return ret;
}

static int bq27z561_read_current(struct bq27z561_mca *bq, u8 reg, int *value)
{
	u16 raw;
	int signed_current;
	int ret = bq27z561_read_word(bq, reg, &raw);

	if (ret)
		return ret;

	/* Preserve Xiaomi's MCA sign convention and report microamps. */
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
	*value = (int)raw - 2730; /* 0.1 K -> 0.1 degC */
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
		*value = (int)raw * 1000; /* uAh */
	return ret;
}

static int bq_fg_get_fcc(void *data, int *value)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_FCC, &raw);

	if (!ret)
		*value = (int)raw * 1000; /* uAh */
	return ret;
}

static int bq_fg_get_design_capacity(void *data, int *value)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_DESIGN_CAP, &raw);

	if (!ret)
		*value = (int)raw * 1000; /* uAh */
	return ret;
}

static int bq_fg_get_cycle(void *data, int *value)
{
	struct bq27z561_mca *bq = data;
	u16 raw;
	int ret = bq27z561_read_word(bq, BQ27Z561_REG_CYCLE, &raw);

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
	/* The legacy MCA callback returns an errno, not the flag value. */
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

static int bq27z561_mca_probe(struct i2c_client *client)
{
	struct bq27z561_mca *bq;
	u32 role = FG_IC_MASTER;
	bool ok = false;
	int ret;

	bq = devm_kzalloc(&client->dev, sizeof(*bq), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;

	bq->dev = &client->dev;
	bq->client = client;
	bq->fake_temp = BQ27Z561_FAKE_TEMP_NONE;
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

	ret = platform_fg_ic_ops_register(bq->role, bq, &bq27z561_mca_ops);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to register MCA FG ops\n");

	bq->online = true;
	mca_log_info("registered role=%u addr=0x%02x\n",
		     bq->role, client->addr);
	return 0;
}

static void bq27z561_mca_remove(struct i2c_client *client)
{
	struct bq27z561_mca *bq = i2c_get_clientdata(client);

	if (bq)
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

MODULE_DESCRIPTION("Xiaomi Dada MCA BQ27Z561 fuel-gauge bring-up driver");
MODULE_LICENSE("GPL v2");
