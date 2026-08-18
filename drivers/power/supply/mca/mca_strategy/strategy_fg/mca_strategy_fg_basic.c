// SPDX-License-Identifier: GPL-2.0
/*
 * Dada fuel-gauge strategy bring-up.
 *
 * Keep the first wired-charging milestone deliberately small: expose the
 * master BQ27Z561 through Xiaomi's strategy_fg ABI.  The original Xiaomi
 * policy adds SOC smoothing, shutdown, authentication, dual-pack and aging
 * logic; those layers can be restored independently without changing the
 * business/power-supply ABI established here.
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/strategy/strategy_fg_class.h>

struct dada_strategy_fg {
	struct device *dev;
	const char *model_name;
	bool charging_done;
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
	int ret = platform_fg_ops_get_volt(FG_IC_MASTER, volt);

	/* BQ27Z561 reports mV; power_supply consumers expect uV later. */
	return ret;
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

	/* Decimal SOC is cosmetic. Keep integer SOC usable if unsupported. */
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

static int dada_strategy_fg_probe(struct platform_device *pdev)
{
	struct dada_strategy_fg *fg;
	const char *name;

	fg = devm_kzalloc(&pdev->dev, sizeof(*fg), GFP_KERNEL);
	if (!fg)
		return -ENOMEM;
	fg->dev = &pdev->dev;
	fg->model_name = "Xiaomi Battery";
	if (!of_property_read_string(pdev->dev.of_node, "model-name", &name))
		fg->model_name = name;
	platform_set_drvdata(pdev, fg);
	return strategy_class_fg_ops_register(fg, &dada_fg_ops);
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
};
module_platform_driver(dada_strategy_fg_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA single-pack fuel-gauge strategy");
MODULE_LICENSE("GPL v2");
