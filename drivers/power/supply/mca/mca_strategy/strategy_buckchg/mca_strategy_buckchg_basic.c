// SPDX-License-Identifier: GPL-2.0
/*
 * Dada MCA buck charging strategy bring-up.
 *
 * Register the stock mca,strategy_buckchg DT node and expose the wired charger
 * state through the common strategy registry.  Policy is intentionally
 * conservative: discovery never changes current/voltage limits by itself;
 * explicit STRATEGY_CONFIG_INPUT_CURRENT_LIMIT requests are forwarded to the
 * restored buck charger class.
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/strategy/strategy_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "strategy_buckchg"
#endif

struct dada_buck_strategy {
	struct device *dev;
	struct notifier_block connect_nb;
	struct notifier_block type_nb;
	bool online;
	int charge_type;
	unsigned int max_power;
};

static void dada_buck_refresh(struct dada_buck_strategy *st)
{
	int online = 0;
	unsigned int type = XM_CHARGER_TYPE_UNKNOW;
	unsigned int power = 0;

	if (!platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &online))
		st->online = !!online;

	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PPS, &type) &&
	    type != XM_CHARGER_TYPE_UNKNOW) {
		st->charge_type = type;
		if (!protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PPS,
							   &power))
			st->max_power = power;
		return;
	}

	type = XM_CHARGER_TYPE_UNKNOW;
	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD, &type) &&
	    type != XM_CHARGER_TYPE_UNKNOW) {
		st->charge_type = type;
		if (!protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PD,
							   &power))
			st->max_power = power;
		return;
	}

	st->charge_type = XM_CHARGER_TYPE_UNKNOW;
	st->max_power = 0;
}

static int dada_buck_strategy_process(int event, int value, void *data)
{
	struct dada_buck_strategy *st = data;

	switch (event) {
	case MCA_EVENT_USB_CONNECT:
		st->online = true;
		dada_buck_refresh(st);
		break;
	case MCA_EVENT_USB_DISCONNECT:
		st->online = false;
		st->charge_type = XM_CHARGER_TYPE_UNKNOW;
		st->max_power = 0;
		break;
	case MCA_EVENT_CHARGE_TYPE_CHANGE:
		dada_buck_refresh(st);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int dada_buck_strategy_get_status(int status, void *value, void *data)
{
	struct dada_buck_strategy *st = data;
	int chg_status = 0;
	bool done = false;

	if (!value)
		return -EINVAL;
	dada_buck_refresh(st);

	switch (status) {
	case STRATEGY_STATUS_TYPE_ONLINE:
		*(bool *)value = st->online;
		return 0;
	case STRATEGY_STATUS_TYPE_CHARGING:
		if (!platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER,
								 &chg_status)) {
			*(int *)value = chg_status;
			return 0;
		}
		if (!platform_class_buckchg_ops_is_charge_done(MAIN_BUCK_CHARGER,
								  &done)) {
			*(int *)value = done ? MCA_BUCK_CHG_STS_CHARGE_DONE :
				MCA_BUCK_CHG_STS_CHARGING;
			return 0;
		}
		return -EOPNOTSUPP;
	case STRATEGY_STATUS_TYPE_QC_TYPE:
		*(int *)value = st->charge_type;
		return 0;
	case STRATEGY_STATUS_TYPE_POWER_MAX:
		*(unsigned int *)value = st->max_power;
		return 0;
	case STRATEGY_STATUS_TYPE_ENABLE:
		*(bool *)value = st->online;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int dada_buck_strategy_set_config(int config, int value, void *data)
{
	if (value < 0)
		return -EINVAL;

	switch (config) {
	case STRATEGY_CONFIG_INPUT_CURRENT_LIMIT:
		return platform_class_buckchg_ops_set_input_curr_lmt(
			MAIN_BUCK_CHARGER, value);
	default:
		return -EOPNOTSUPP;
	}
}

static int dada_buck_connect_event(struct notifier_block *nb,
				   unsigned long event, void *data)
{
	struct dada_buck_strategy *st =
		container_of(nb, struct dada_buck_strategy, connect_nb);

	return dada_buck_strategy_process(event, 0, st) ? NOTIFY_DONE : NOTIFY_OK;
}

static int dada_buck_type_event(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct dada_buck_strategy *st =
		container_of(nb, struct dada_buck_strategy, type_nb);

	if (event != MCA_EVENT_CHARGE_TYPE_CHANGE)
		return NOTIFY_DONE;
	dada_buck_refresh(st);
	return NOTIFY_OK;
}

static int dada_buck_strategy_probe(struct platform_device *pdev)
{
	struct dada_buck_strategy *st;
	int ret;

	st = devm_kzalloc(&pdev->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;
	st->dev = &pdev->dev;
	st->charge_type = XM_CHARGER_TYPE_UNKNOW;
	platform_set_drvdata(pdev, st);

	ret = mca_strategy_ops_register(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					mca_strategy_func(dada_buck_strategy_process),
					dada_buck_strategy_get_status,
					dada_buck_strategy_set_config, st);
	if (ret)
		return ret;

	st->connect_nb.notifier_call = dada_buck_connect_event;
	st->type_nb.notifier_call = dada_buck_type_event;
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
					      &st->connect_nb);
	if (ret)
		mca_log_info("connect notifier unavailable: %d\n", ret);
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGE_TYPE,
					      &st->type_nb);
	if (ret)
		mca_log_info("type notifier unavailable: %d\n", ret);

	dada_buck_refresh(st);
	mca_log_info("buck strategy registered online=%d type=%d power=%u\n",
		     st->online, st->charge_type, st->max_power);
	return 0;
}

static int dada_buck_strategy_remove(struct platform_device *pdev)
{
	struct dada_buck_strategy *st = platform_get_drvdata(pdev);

	if (!st)
		return 0;
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGE_TYPE,
					  &st->type_nb);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
					  &st->connect_nb);
	return 0;
}

static const struct of_device_id dada_buck_strategy_match[] = {
	{ .compatible = "mca,strategy_buckchg" },
	{ .compatible = "xiaomi,strategy_buckchg" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_buck_strategy_match);

static struct platform_driver dada_buck_strategy_driver = {
	.driver = {
		.name = "mca_strategy_buckchg",
		.of_match_table = dada_buck_strategy_match,
	},
	.probe = dada_buck_strategy_probe,
	.remove = dada_buck_strategy_remove,
};
module_platform_driver(dada_buck_strategy_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA conservative buck charging strategy");
MODULE_LICENSE("GPL v2");
