// SPDX-License-Identifier: GPL-2.0
/*
 * Dada MCA battery business layer.
 *
 * This establishes the Android/Linux battery power_supply ABI on top of the
 * restored fuel-gauge strategy. Charger-policy-specific writable controls are
 * deliberately kept out until the buck charger strategy is connected.
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/strategy/strategy_fg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "business_battery"
#endif

struct dada_battery_business {
	struct device *dev;
	struct power_supply *psy;
	struct power_supply_desc desc;
	struct notifier_block event_nb;
};

static enum power_supply_property dada_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_MODEL_NAME,
};

static bool dada_source_online(const char *name)
{
	struct power_supply *psy;
	union power_supply_propval pval = { 0 };
	bool online = false;

	psy = power_supply_get_by_name(name);
	if (!psy)
		return false;
	if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &pval))
		online = !!pval.intval;
	power_supply_put(psy);
	return online;
}

static int dada_battery_get_status(union power_supply_propval *val)
{
	int battery_current = 0;
	int soc;
	bool online;

	online = dada_source_online("usb") || dada_source_online("wireless");
	soc = strategy_class_fg_ops_get_soc();
	if (online) {
		if (soc == 100 && strategy_class_fg_ops_get_charging_done())
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		return 0;
	}

	/* Early bring-up fallback for platforms whose USB psy appears later. */
	if (!strategy_class_fg_ops_get_current(&battery_current) &&
	    battery_current > 0)
		val->intval = POWER_SUPPLY_STATUS_CHARGING;
	else
		val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
	return 0;
}

static int dada_battery_capacity_level(int soc)
{
	if (soc <= 0)
		return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	if (soc <= 20)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	if (soc <= 80)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	if (soc < 100)
		return POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	return POWER_SUPPLY_CAPACITY_LEVEL_FULL;
}

static int dada_battery_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	int ret = 0;
	int soc;
	const char *name;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return dada_battery_get_status(val);
	case POWER_SUPPLY_PROP_HEALTH:
		ret = strategy_class_fg_get_health(&val->intval);
		if (ret)
			val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
		return 0;
	case POWER_SUPPLY_PROP_PRESENT:
		/* Dada has a non-removable single battery pack. */
		val->intval = 1;
		return 0;
	case POWER_SUPPLY_PROP_CAPACITY:
		soc = strategy_class_fg_ops_get_soc();
		if (soc < 0 || soc > 100)
			return -ENODATA;
		val->intval = soc;
		return 0;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		soc = strategy_class_fg_ops_get_soc();
		if (soc < 0 || soc > 100)
			return -ENODATA;
		val->intval = dada_battery_capacity_level(soc);
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		ret = strategy_class_fg_ops_get_voltage(&val->intval);
		if (!ret)
			val->intval *= 1000; /* mV -> uV */
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = strategy_class_fg_ops_get_current(&val->intval);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = strategy_class_fg_ops_get_temperature(&val->intval);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		ret = strategy_class_fg_get_rm(&val->intval);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		ret = strategy_class_fg_ops_get_cyclecount(&val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = strategy_class_fg_get_fcc(&val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = strategy_class_fg_get_dc(&val->intval);
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		ret = strategy_class_fg_get_model_name(&name);
		if (!ret)
			val->strval = name;
		break;
	default:
		return -EINVAL;
	}

	return ret ? -ENODATA : 0;
}

static int dada_battery_event(struct notifier_block *nb,
			      unsigned long event, void *data)
{
	struct dada_battery_business *battery =
		container_of(nb, struct dada_battery_business, event_nb);

	if (battery->psy)
		power_supply_changed(battery->psy);
	return NOTIFY_OK;
}

static int dada_battery_register_notifiers(struct dada_battery_business *battery)
{
	int ret;

	battery->event_nb.notifier_call = dada_battery_event;
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
					      &battery->event_nb);
	if (ret)
		return ret;
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_BATTERY_INFO,
					      &battery->event_nb);
	if (ret)
		goto err_charger;
	ret = mca_event_block_notify_register(MCA_EVENT_CHARGE_STATUS,
					      &battery->event_nb);
	if (ret)
		goto err_battery;
	return 0;

err_battery:
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_BATTERY_INFO,
					  &battery->event_nb);
err_charger:
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
					  &battery->event_nb);
	return ret;
}

static void dada_battery_unregister_notifiers(struct dada_battery_business *battery)
{
	mca_event_block_notify_unregister(MCA_EVENT_CHARGE_STATUS,
					  &battery->event_nb);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_BATTERY_INFO,
					  &battery->event_nb);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
					  &battery->event_nb);
}

static int dada_battery_probe(struct platform_device *pdev)
{
	struct dada_battery_business *battery;
	struct power_supply_config cfg = { 0 };
	int ret;

	battery = devm_kzalloc(&pdev->dev, sizeof(*battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;
	battery->dev = &pdev->dev;
	battery->desc.name = "battery";
	battery->desc.type = POWER_SUPPLY_TYPE_BATTERY;
	battery->desc.properties = dada_battery_props;
	battery->desc.num_properties = ARRAY_SIZE(dada_battery_props);
	battery->desc.get_property = dada_battery_get_property;

	cfg.drv_data = battery;
	cfg.of_node = pdev->dev.of_node;
	battery->psy = devm_power_supply_register(&pdev->dev, &battery->desc, &cfg);
	if (IS_ERR(battery->psy))
		return dev_err_probe(&pdev->dev, PTR_ERR(battery->psy),
				     "failed to register battery power_supply\n");

	platform_set_drvdata(pdev, battery);
	ret = dada_battery_register_notifiers(battery);
	if (ret)
		mca_log_info("event notifier registration deferred: %d\n", ret);

	mca_log_info("battery power_supply registered\n");
	return 0;
}

static int dada_battery_remove(struct platform_device *pdev)
{
	struct dada_battery_business *battery = platform_get_drvdata(pdev);

	if (battery)
		dada_battery_unregister_notifiers(battery);
	return 0;
}

static const struct of_device_id dada_battery_match[] = {
	{ .compatible = "mca,business_battery" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_battery_match);

static struct platform_driver dada_battery_driver = {
	.driver = {
		.name = "mca_business_battery",
		.of_match_table = dada_battery_match,
	},
	.probe = dada_battery_probe,
	.remove = dada_battery_remove,
};
module_platform_driver(dada_battery_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA battery business layer");
MODULE_LICENSE("GPL v2");
