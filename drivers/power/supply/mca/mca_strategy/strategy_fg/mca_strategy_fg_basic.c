// SPDX-License-Identifier: GPL-2.0
/*
 * Dada single-pack fuel-gauge strategy.
 *
 * Keep the active charging policy conservative while exposing the stock Dada
 * strategy_fg userspace ABI consumed by Xiaomi's micharge service.
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/string.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/strategy/strategy_fg_class.h>

struct dada_strategy_fg {
	struct device *dev;
	struct device *sysfs_dev;
	const char *model_name;
	bool charging_done;
	int enable_rollback;
};

enum dada_strategy_fg_sysfs_attr {
	DADA_FG_ATTR_AUTHENTIC = 0,
	DADA_FG_ATTR_SLAVE_AUTHENTIC,
	DADA_FG_ATTR_FAST_CHARGE,
	DADA_FG_ATTR_SOC_DECIMAL,
	DADA_FG_ATTR_SOC_DECIMAL_RATE,
	DADA_FG_ATTR_BATTERY_NUM,
	DADA_FG_ATTR_ENABLE_ROLLBACK,
	DADA_FG_ATTR_CALC_RVALUE,
	DADA_FG_ATTR_MANUFACTURING_DATE,
	DADA_FG_ATTR_FIRST_USAGE_DATE,
};

static int dada_fg_is_init_ok(void *data)
{
	bool ok = false;
	int ret = platform_fg_ops_probe_ok(FG_IC_MASTER, &ok);

	if (ret)
		return ret;
	return ok ? 0 : -ENODEV;
}

static int dada_fg_get_rsoc(void *data, int *rsoc)
{
	return platform_fg_ops_get_rsoc(FG_IC_MASTER, rsoc);
}

static int dada_fg_get_soc(void *data)
{
	int soc = 0;
	int ret = platform_fg_ops_get_rsoc(FG_IC_MASTER, &soc);

	return ret ? ret : clamp_val(soc, 0, 100);
}

static int dada_fg_get_temp(void *data, int *temp)
{
	return platform_fg_ops_get_temp(FG_IC_MASTER, temp);
}

static int dada_fg_get_current(void *data, int *curr)
{
	return platform_fg_ops_get_curr(FG_IC_MASTER, curr);
}

static int dada_fg_get_voltage(void *data, int *volt)
{
	return platform_fg_ops_get_volt(FG_IC_MASTER, volt);
}

static int dada_fg_get_cycle(void *data, int *cycle)
{
	return platform_fg_ops_get_cyclecount(FG_IC_MASTER, cycle);
}

static int dada_fg_get_voltage_mean(void *data, int *volt)
{
	return dada_fg_get_voltage(data, volt);
}

static int dada_fg_get_soc_decimal(void *data, int *decimal, int *rate)
{
	int ret_decimal, ret_rate;

	ret_decimal = platform_fg_ops_get_decimal(FG_IC_MASTER, decimal);
	ret_rate = platform_fg_ops_get_decimal_rate(FG_IC_MASTER, rate);
	if (!ret_decimal && !ret_rate)
		return 0;

	/* Decimal SOC is display-only; integer SOC remains authoritative. */
	*decimal = 0;
	*rate = 0;
	return 0;
}

static bool dada_fg_get_charging_done(void *data)
{
	return ((struct dada_strategy_fg *)data)->charging_done;
}

static int dada_fg_set_charging_done(void *data, bool done)
{
	((struct dada_strategy_fg *)data)->charging_done = done;
	return 0;
}

static int dada_fg_get_model_name(void *data, const char **name)
{
	struct dada_strategy_fg *fg = data;
	const char *ic_name = NULL;

	if (!name)
		return -EINVAL;
	if (!platform_fg_ops_get_device_name(FG_IC_MASTER, &ic_name) && ic_name) {
		*name = ic_name;
		return 0;
	}
	*name = fg->model_name;
	return 0;
}

static int dada_fg_set_fastcharge(void *data, bool en)
{
	return platform_fg_ops_set_fastcharge(FG_IC_MASTER, en);
}

static int dada_fg_get_fastcharge(void *data)
{
	int fastcharge = 0;
	int ret = platform_fg_ops_get_fastcharge(FG_IC_MASTER, &fastcharge);

	return ret ? ret : fastcharge;
}

static int dada_fg_get_authentic(void *data, bool *authentic)
{
	int auth = 0;
	int ret;

	if (!authentic)
		return -EINVAL;
	ret = platform_fg_ops_get_authentic(FG_IC_MASTER, &auth);
	if (ret) {
		*authentic = false;
		return ret;
	}
	*authentic = !!auth;
	return 0;
}

static int dada_fg_get_dc(void *data, int *dc)
{
	return platform_fg_ops_get_full_design(FG_IC_MASTER, dc);
}

static int dada_fg_get_rm(void *data, int *rm)
{
	return platform_fg_ops_get_rm(FG_IC_MASTER, rm);
}

static int dada_fg_get_fcc(void *data, int *fcc)
{
	return platform_fg_ops_get_fcc(FG_IC_MASTER, fcc);
}

static int dada_fg_is_chip_ok(void *data)
{
	int ok = 0;
	int ret = platform_fg_ops_get_chip_ok(FG_IC_MASTER, &ok);

	return ret ? ret : (ok ? 0 : -ENODEV);
}

static int dada_fg_get_health(void *data, int *health)
{
	int temp = 250;
	int ret;

	if (!health)
		return -EINVAL;
	ret = dada_fg_is_chip_ok(data);
	if (ret) {
		*health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		return 0;
	}
	if (dada_fg_get_temp(data, &temp)) {
		*health = POWER_SUPPLY_HEALTH_UNKNOWN;
		return 0;
	}
	if (temp >= 600)
		*health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (temp <= -100)
		*health = POWER_SUPPLY_HEALTH_COLD;
	else
		*health = POWER_SUPPLY_HEALTH_GOOD;
	return 0;
}

static int dada_fg_get_first_termination(void *data, int *value)
{
	if (!value)
		return -EINVAL;
	*value = 0;
	return 0;
}

static int dada_fg_get_pack_vendor(void *data, int *vendor)
{
	return platform_fg_ops_get_pack_vendor(FG_IC_MASTER, vendor);
}

static int dada_fg_dual_is_chip_ok(void *data, int index)
{
	int ok = 0;
	int ret;

	if (index < 0 || index >= FG_IC_MAX)
		return -EINVAL;
	ret = platform_fg_ops_get_chip_ok(index, &ok);
	return ret ? ret : (ok ? 0 : -ENODEV);
}

static int dada_fg_get_thermal_temp(void *data, int *temp)
{
	return dada_fg_get_temp(data, temp);
}

static int dada_fg_get_soh(void *data, int *soh)
{
	return platform_fg_ops_get_soh(FG_IC_MASTER, soh);
}

static int dada_fg_get_temp_offset_flag(void *data, int *flag)
{
	if (!flag)
		return -EINVAL;
	*flag = 0;
	return 0;
}

static struct strategy_fg_class_ops dada_fg_ops = {
	.strategy_fg_is_init_ok = dada_fg_is_init_ok,
	.strategy_fg_get_rsoc = dada_fg_get_rsoc,
	.strategy_fg_get_soc = dada_fg_get_soc,
	.strategy_fg_get_temp = dada_fg_get_temp,
	.strategy_fg_get_current = dada_fg_get_current,
	.strategy_fg_get_voltage = dada_fg_get_voltage,
	.strategy_fg_get_cycle = dada_fg_get_cycle,
	.strategy_fg_get_voltage_mean = dada_fg_get_voltage_mean,
	.strategy_fg_get_soc_decimal_info = dada_fg_get_soc_decimal,
	.strategy_fg_get_charging_done = dada_fg_get_charging_done,
	.strategy_fg_set_charging_done = dada_fg_set_charging_done,
	.strategy_fg_get_model_name = dada_fg_get_model_name,
	.strategy_fg_set_fastcharge = dada_fg_set_fastcharge,
	.strategy_fg_get_fastcharge = dada_fg_get_fastcharge,
	.strategy_fg_get_authentic = dada_fg_get_authentic,
	.strategy_fg_get_dc = dada_fg_get_dc,
	.strategy_fg_get_rm = dada_fg_get_rm,
	.strategy_fg_get_fcc = dada_fg_get_fcc,
	.strategy_fg_is_chip_ok = dada_fg_is_chip_ok,
	.strategy_fg_get_health = dada_fg_get_health,
	.strategy_fg_get_first_termination = dada_fg_get_first_termination,
	.strategy_fg_get_pack_vendor_id = dada_fg_get_pack_vendor,
	.strategy_fg_dual_is_chip_ok = dada_fg_dual_is_chip_ok,
	.strategy_fg_get_thermal_temperature = dada_fg_get_thermal_temp,
	.strategy_fg_get_soh = dada_fg_get_soh,
	.strategy_fg_get_temp_offset_flag = dada_fg_get_temp_offset_flag,
};

static ssize_t dada_strategy_fg_sysfs_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf);
static ssize_t dada_strategy_fg_sysfs_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count);

static struct mca_sysfs_attr_info dada_strategy_fg_sysfs_fields[] = {
	mca_sysfs_attr_rw(dada_strategy_fg_sysfs, 0664,
			  DADA_FG_ATTR_AUTHENTIC, authentic),
	mca_sysfs_attr_rw(dada_strategy_fg_sysfs, 0664,
			  DADA_FG_ATTR_SLAVE_AUTHENTIC, slave_authentic),
	mca_sysfs_attr_ro(dada_strategy_fg_sysfs, 0440,
			  DADA_FG_ATTR_FAST_CHARGE, fast_charge),
	mca_sysfs_attr_ro(dada_strategy_fg_sysfs, 0440,
			  DADA_FG_ATTR_SOC_DECIMAL, soc_decimal),
	mca_sysfs_attr_ro(dada_strategy_fg_sysfs, 0440,
			  DADA_FG_ATTR_SOC_DECIMAL_RATE, soc_decimal_rate),
	mca_sysfs_attr_ro(dada_strategy_fg_sysfs, 0440,
			  DADA_FG_ATTR_BATTERY_NUM, battery_num),
	mca_sysfs_attr_rw(dada_strategy_fg_sysfs, 0664,
			  DADA_FG_ATTR_ENABLE_ROLLBACK, enable_rollback),
	mca_sysfs_attr_ro(dada_strategy_fg_sysfs, 0440,
			  DADA_FG_ATTR_CALC_RVALUE, calc_rvalue),
	mca_sysfs_attr_ro(dada_strategy_fg_sysfs, 0440,
			  DADA_FG_ATTR_MANUFACTURING_DATE, manufacturing_date),
	mca_sysfs_attr_rw(dada_strategy_fg_sysfs, 0640,
			  DADA_FG_ATTR_FIRST_USAGE_DATE, first_usage_date),
};

#define DADA_FG_SYSFS_ATTR_COUNT ARRAY_SIZE(dada_strategy_fg_sysfs_fields)
static struct attribute *dada_strategy_fg_sysfs_attrs[DADA_FG_SYSFS_ATTR_COUNT + 1];
static const struct attribute_group dada_strategy_fg_sysfs_group = {
	.attrs = dada_strategy_fg_sysfs_attrs,
};

static struct dada_strategy_fg *dada_strategy_fg_from_attr(
	struct device *dev, struct device_attribute *attr,
	struct mca_sysfs_attr_info **field)
{
	*field = mca_sysfs_lookup_attr(attr->attr.name,
				       dada_strategy_fg_sysfs_fields,
				       DADA_FG_SYSFS_ATTR_COUNT);
	return *field ? dev_get_drvdata(dev) : NULL;
}

static ssize_t dada_strategy_fg_show_date(char *buf, bool first_usage)
{
	u8 date[16] = { 0 };
	int ret;

	if (first_usage)
		ret = platform_fg_ops_get_first_usage_date(FG_IC_MASTER, date);
	else
		ret = platform_fg_ops_get_manufacturing_date(FG_IC_MASTER, date);
	if (ret)
		return sysfs_emit(buf, "\n");
	date[sizeof(date) - 1] = '\0';
	return sysfs_emit(buf, "%s\n", date);
}

static ssize_t dada_strategy_fg_sysfs_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct mca_sysfs_attr_info *field;
	struct dada_strategy_fg *fg = dada_strategy_fg_from_attr(dev, attr, &field);
	unsigned long rvalue;
	int value = 0, decimal = 0, rate = 0;
	int ret;

	if (!fg)
		return -ENODEV;

	switch (field->sysfs_attr_name) {
	case DADA_FG_ATTR_AUTHENTIC:
		ret = platform_fg_ops_get_authentic(FG_IC_MASTER, &value);
		if (ret)
			value = 0;
		return sysfs_emit(buf, "%d\n", !!value);
	case DADA_FG_ATTR_SLAVE_AUTHENTIC:
		/* Dada is a single-pack design; stock userspace still probes the file. */
		ret = platform_fg_ops_get_authentic(FG_IC_SLAVE, &value);
		if (ret)
			value = 0;
		return sysfs_emit(buf, "%d\n", !!value);
	case DADA_FG_ATTR_FAST_CHARGE:
		ret = platform_fg_ops_get_fastcharge(FG_IC_MASTER, &value);
		if (ret)
			value = 0;
		return sysfs_emit(buf, "%d\n", !!value);
	case DADA_FG_ATTR_SOC_DECIMAL:
		dada_fg_get_soc_decimal(fg, &decimal, &rate);
		return sysfs_emit(buf, "%d\n", decimal);
	case DADA_FG_ATTR_SOC_DECIMAL_RATE:
		dada_fg_get_soc_decimal(fg, &decimal, &rate);
		return sysfs_emit(buf, "%d\n", rate);
	case DADA_FG_ATTR_BATTERY_NUM:
		/* Stock strategy returns 0 for a single gauge and 1 for parallel packs. */
		return sysfs_emit(buf, "0\n");
	case DADA_FG_ATTR_ENABLE_ROLLBACK:
		return sysfs_emit(buf, "%d\n", fg->enable_rollback);
	case DADA_FG_ATTR_CALC_RVALUE:
		rvalue = platform_fg_ops_get_calc_rvalue(FG_IC_MASTER);
		return sysfs_emit(buf, "%lu\n", rvalue);
	case DADA_FG_ATTR_MANUFACTURING_DATE:
		return dada_strategy_fg_show_date(buf, false);
	case DADA_FG_ATTR_FIRST_USAGE_DATE:
		return dada_strategy_fg_show_date(buf, true);
	default:
		return -EINVAL;
	}
}

static ssize_t dada_strategy_fg_sysfs_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *field;
	struct dada_strategy_fg *fg = dada_strategy_fg_from_attr(dev, attr, &field);
	char date[16];
	int value;
	int ret;

	if (!fg)
		return -ENODEV;

	switch (field->sysfs_attr_name) {
	case DADA_FG_ATTR_AUTHENTIC:
		if (kstrtoint(buf, 10, &value))
			return -EINVAL;
		ret = platform_fg_ops_set_authentic(FG_IC_MASTER, !!value);
		return ret ? ret : count;
	case DADA_FG_ATTR_SLAVE_AUTHENTIC:
		if (kstrtoint(buf, 10, &value))
			return -EINVAL;
		ret = platform_fg_ops_set_authentic(FG_IC_SLAVE, !!value);
		/* No slave is populated on Dada; preserve the ABI as a harmless write. */
		return ret == -EOPNOTSUPP || ret == -ENODEV ? count : (ret ? ret : count);
	case DADA_FG_ATTR_ENABLE_ROLLBACK:
		if (kstrtoint(buf, 10, &value))
			return -EINVAL;
		fg->enable_rollback = !!value;
		return count;
	case DADA_FG_ATTR_FIRST_USAGE_DATE:
		if (!count || count >= sizeof(date))
			return -EINVAL;
		memcpy(date, buf, count);
		date[count] = '\0';
		strim(date);
		platform_fg_ops_set_first_usage_date(FG_IC_MASTER, date);
		return count;
	default:
		return -EACCES;
	}
}

static int dada_strategy_fg_sysfs_create(struct platform_device *pdev,
					 struct dada_strategy_fg *fg)
{
	int ret;

	mca_sysfs_init_attrs(dada_strategy_fg_sysfs_attrs,
			     dada_strategy_fg_sysfs_fields,
			     DADA_FG_SYSFS_ATTR_COUNT);
	ret = mca_sysfs_create_link_group(SYSFS_DEV_2, "strategy_fg",
					  &pdev->dev, &dada_strategy_fg_sysfs_group);
	if (!ret)
		fg->sysfs_dev = &pdev->dev;
	return ret;
}

static int dada_strategy_fg_probe(struct platform_device *pdev)
{
	struct dada_strategy_fg *fg;
	const char *name;
	int ret;

	fg = devm_kzalloc(&pdev->dev, sizeof(*fg), GFP_KERNEL);
	if (!fg)
		return -ENOMEM;
	fg->dev = &pdev->dev;
	fg->model_name = "Xiaomi Battery";
	if (!of_property_read_string(pdev->dev.of_node, "model-name", &name))
		fg->model_name = name;
	platform_set_drvdata(pdev, fg);

	ret = strategy_class_fg_ops_register(fg, &dada_fg_ops);
	if (ret)
		return ret;

	ret = dada_strategy_fg_sysfs_create(pdev, fg);
	if (ret)
		dev_warn(&pdev->dev, "strategy_fg sysfs unavailable: %d\n", ret);
	return 0;
}

static int dada_strategy_fg_remove(struct platform_device *pdev)
{
	struct dada_strategy_fg *fg = platform_get_drvdata(pdev);

	if (fg && fg->sysfs_dev)
		mca_sysfs_remove_link_group(SYSFS_DEV_2, "strategy_fg",
					    &pdev->dev, &dada_strategy_fg_sysfs_group);
	return 0;
}

static const struct of_device_id dada_strategy_fg_match[] = {
	{ .compatible = "xiaomi,strategy_fg" },
	{ .compatible = "mca,strategy_fg" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_strategy_fg_match);

static struct platform_driver dada_strategy_fg_driver = {
	.driver = {
		.name = "mca_strategy_fg",
		.of_match_table = dada_strategy_fg_match,
	},
	.probe = dada_strategy_fg_probe,
	.remove = dada_strategy_fg_remove,
};
module_platform_driver(dada_strategy_fg_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA single-pack fuel-gauge strategy");
MODULE_LICENSE("GPL v2");
