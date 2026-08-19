// SPDX-License-Identifier: GPL-2.0
/*
 * Dada MCA buck charging strategy bring-up.
 *
 * State discovery remains passive. Explicit userspace control is serialized
 * through MCA votables before reaching the Qualcomm sub-PMIC ops, matching
 * the stock multi-client charging-interface model.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <mca/common/mca_charge_interface.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_voter.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/strategy/strategy_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "strategy_buckchg"
#endif

#define DADA_BUCK_IIN_DEFAULT INT_MAX
#define DADA_BUCK_ICHG_DEFAULT INT_MAX
#define DADA_BUCK_POWER_DEFAULT INT_MAX

struct dada_buck_strategy {
	struct device *dev;
	struct notifier_block connect_nb;
	struct notifier_block type_nb;
	struct mca_votable *input_suspend_voter;
	struct mca_votable *charge_disable_voter;
	struct mca_votable *input_limit_voter;
	struct mca_votable *charge_limit_voter;
	struct mca_votable *power_limit_voter;
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
		if (!protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PPS, &power))
			st->max_power = power;
		return;
	}

	type = XM_CHARGER_TYPE_UNKNOW;
	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD, &type) &&
	    type != XM_CHARGER_TYPE_UNKNOW) {
		st->charge_type = type;
		if (!protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PD, &power))
			st->max_power = power;
		return;
	}

	st->charge_type = XM_CHARGER_TYPE_UNKNOW;
	st->max_power = 0;
}

static int dada_buck_suspend_vote_cb(struct mca_votable *votable, void *data,
				     int result, const char *client)
{
	return platform_class_buckchg_ops_set_hiz(MAIN_BUCK_CHARGER, !!result);
}

static int dada_buck_disable_vote_cb(struct mca_votable *votable, void *data,
				     int result, const char *client)
{
	return platform_class_buckchg_ops_set_chg(MAIN_BUCK_CHARGER, !result);
}

static int dada_buck_iin_vote_cb(struct mca_votable *votable, void *data,
				 int result, const char *client)
{
	if (result == DADA_BUCK_IIN_DEFAULT)
		return 0;
	if (result < 0)
		return -EINVAL;
	return platform_class_buckchg_ops_set_input_curr_lmt(MAIN_BUCK_CHARGER,
							      result);
}

static int dada_buck_ichg_vote_cb(struct mca_votable *votable, void *data,
				  int result, const char *client)
{
	if (result == DADA_BUCK_ICHG_DEFAULT)
		return 0;
	if (result < 0)
		return -EINVAL;
	return platform_class_buckchg_ops_set_ichg(MAIN_BUCK_CHARGER, result);
}

static int dada_buck_power_vote_cb(struct mca_votable *votable, void *data,
				   int result, const char *client)
{
	/*
	 * Keep power-limit voting state ABI-complete without inventing a power->ICL
	 * conversion. Stock policy combines this vote with adapter/thermal state.
	 */
	return 0;
}

static int dada_buck_create_voters(struct dada_buck_strategy *st)
{
	st->input_suspend_voter = mca_create_votable(
		"DADA_BUCK_INPUT_SUSPEND", MCA_VOTE_OR,
		dada_buck_suspend_vote_cb, 0, st);
	if (IS_ERR(st->input_suspend_voter))
		return PTR_ERR(st->input_suspend_voter);

	st->charge_disable_voter = mca_create_votable(
		"DADA_BUCK_CHARGE_DISABLE", MCA_VOTE_OR,
		dada_buck_disable_vote_cb, 0, st);
	if (IS_ERR(st->charge_disable_voter))
		return PTR_ERR(st->charge_disable_voter);

	st->input_limit_voter = mca_create_votable(
		"DADA_BUCK_INPUT_LIMIT", MCA_VOTE_MIN,
		dada_buck_iin_vote_cb, DADA_BUCK_IIN_DEFAULT, st);
	if (IS_ERR(st->input_limit_voter))
		return PTR_ERR(st->input_limit_voter);

	st->charge_limit_voter = mca_create_votable(
		"DADA_BUCK_CHARGE_LIMIT", MCA_VOTE_MIN,
		dada_buck_ichg_vote_cb, DADA_BUCK_ICHG_DEFAULT, st);
	if (IS_ERR(st->charge_limit_voter))
		return PTR_ERR(st->charge_limit_voter);

	st->power_limit_voter = mca_create_votable(
		"DADA_BUCK_POWER_LIMIT", MCA_VOTE_MIN,
		dada_buck_power_vote_cb, DADA_BUCK_POWER_DEFAULT, st);
	if (IS_ERR(st->power_limit_voter))
		return PTR_ERR(st->power_limit_voter);
	return 0;
}

static void dada_buck_destroy_voters(struct dada_buck_strategy *st)
{
	if (!IS_ERR_OR_NULL(st->power_limit_voter))
		mca_destroy_votable(st->power_limit_voter);
	if (!IS_ERR_OR_NULL(st->charge_limit_voter))
		mca_destroy_votable(st->charge_limit_voter);
	if (!IS_ERR_OR_NULL(st->input_limit_voter))
		mca_destroy_votable(st->input_limit_voter);
	if (!IS_ERR_OR_NULL(st->charge_disable_voter))
		mca_destroy_votable(st->charge_disable_voter);
	if (!IS_ERR_OR_NULL(st->input_suspend_voter))
		mca_destroy_votable(st->input_suspend_voter);
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
		*(bool *)value = !mca_get_effective_result(st->charge_disable_voter);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int dada_buck_strategy_set_config(int config, int value, void *data)
{
	struct dada_buck_strategy *st = data;

	if (!st || value < 0)
		return -EINVAL;
	switch (config) {
	case STRATEGY_CONFIG_INPUT_CURRENT_LIMIT:
		return mca_vote(st->input_limit_voter, "strategy_config",
				value != 0, value);
	default:
		return -EOPNOTSUPP;
	}
}

static int dada_buck_if_set_input_suspend(const char *user, char *value,
					  void *data)
{
	struct dada_buck_strategy *st = data;
	int val;

	if (!st || !user || !value || kstrtoint(value, 0, &val))
		return -EINVAL;
	return mca_vote(st->input_suspend_voter, user, true, !!val);
}

static int dada_buck_if_get_input_suspend(char *value, void *data)
{
	struct dada_buck_strategy *st = data;

	if (!st || !value)
		return -EINVAL;
	return scnprintf(value, MCA_CHARGE_IF_MAX_VALUE_BUFF, "%d",
			 mca_get_effective_result(st->input_suspend_voter)) < 0 ?
		-EIO : 0;
}

static int dada_buck_if_set_charge_enable(const char *user, unsigned int value,
					  void *data)
{
	struct dada_buck_strategy *st = data;

	if (!st || !user || value > 1)
		return -EINVAL;
	return mca_vote(st->charge_disable_voter, user, true, !value);
}

static int dada_buck_if_get_charge_enable(char *value, void *data)
{
	struct dada_buck_strategy *st = data;
	int disabled;

	if (!st || !value)
		return -EINVAL;
	disabled = mca_get_effective_result(st->charge_disable_voter);
	scnprintf(value, MCA_CHARGE_IF_MAX_VALUE_BUFF, "%d", !disabled);
	return 0;
}

static int dada_buck_if_set_iin(const char *user, char *value, void *data)
{
	struct dada_buck_strategy *st = data;
	int val;

	if (!st || !user || !value || kstrtoint(value, 0, &val) || val < 0)
		return -EINVAL;
	return mca_vote(st->input_limit_voter, user, val != 0, val);
}

static int dada_buck_if_get_iin(char *value, void *data)
{
	struct dada_buck_strategy *st = data;
	int val;

	if (!st || !value)
		return -EINVAL;
	val = mca_get_effective_result(st->input_limit_voter);
	if (val == DADA_BUCK_IIN_DEFAULT) {
		if (platform_class_buckchg_ops_get_input_curr_lmt(MAIN_BUCK_CHARGER,
							      &val))
			val = 0;
	}
	scnprintf(value, MCA_CHARGE_IF_MAX_VALUE_BUFF, "%d", val);
	return 0;
}

static int dada_buck_if_set_ichg(const char *user, char *value, void *data)
{
	struct dada_buck_strategy *st = data;
	int val;

	if (!st || !user || !value || kstrtoint(value, 0, &val) || val < 0)
		return -EINVAL;
	return mca_vote(st->charge_limit_voter, user, val != 0, val);
}

static int dada_buck_if_get_ichg(char *value, void *data)
{
	struct dada_buck_strategy *st = data;
	int val;

	if (!st || !value)
		return -EINVAL;
	val = mca_get_effective_result(st->charge_limit_voter);
	if (val == DADA_BUCK_ICHG_DEFAULT)
		val = 0;
	scnprintf(value, MCA_CHARGE_IF_MAX_VALUE_BUFF, "%d", val);
	return 0;
}

static int dada_buck_if_set_power(const char *user, unsigned int value,
				  void *data)
{
	struct dada_buck_strategy *st = data;

	if (!st || !user || value > INT_MAX)
		return -EINVAL;
	return mca_vote(st->power_limit_voter, user, value != 0, value);
}

static int dada_buck_if_get_power(char *value, void *data)
{
	struct dada_buck_strategy *st = data;
	int val;

	if (!st || !value)
		return -EINVAL;
	val = mca_get_effective_result(st->power_limit_voter);
	if (val == DADA_BUCK_POWER_DEFAULT)
		val = 0;
	scnprintf(value, MCA_CHARGE_IF_MAX_VALUE_BUFF, "%d", val);
	return 0;
}

static int dada_buck_if_set_shipmode(const char *user, unsigned int value,
				     void *data)
{
	if (value > 1)
		return -EINVAL;
	return platform_class_buckchg_ops_set_ship_mode(MAIN_BUCK_CHARGER,
							 value != 0);
}

static int dada_buck_if_get_shipmode(bool *enabled, void *data)
{
	if (!enabled)
		return -EINVAL;
	return platform_class_buckchg_ops_get_ship_mode(MAIN_BUCK_CHARGER, enabled);
}

static struct mca_charge_if_ops dada_buck_charge_if_ops = {
	.type_name = "buck",
	.set_input_suspend = dada_buck_if_set_input_suspend,
	.set_charge_enable = dada_buck_if_set_charge_enable,
	.set_input_current_limit = dada_buck_if_set_iin,
	.set_charge_current_limit = dada_buck_if_set_ichg,
	.set_charge_power_limit = dada_buck_if_set_power,
	.set_ship_mode_en = dada_buck_if_set_shipmode,
	.get_input_suspend = dada_buck_if_get_input_suspend,
	.get_charge_enable = dada_buck_if_get_charge_enable,
	.get_input_current_limit = dada_buck_if_get_iin,
	.get_charge_current_limit = dada_buck_if_get_ichg,
	.get_charge_power_limit = dada_buck_if_get_power,
	.get_ship_mode_status = dada_buck_if_get_shipmode,
};

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

	ret = dada_buck_create_voters(st);
	if (ret)
		goto err_voters;
	ret = mca_strategy_ops_register(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					dada_buck_strategy_process,
					dada_buck_strategy_get_status,
					dada_buck_strategy_set_config, st);
	if (ret)
		goto err_voters;

	dada_buck_charge_if_ops.data = st;
	ret = mca_charge_if_ops_register(&dada_buck_charge_if_ops);
	if (ret)
		goto err_voters;

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
	mca_log_info("buck strategy/interface registered online=%d type=%d power=%u\n",
		     st->online, st->charge_type, st->max_power);
	return 0;

err_voters:
	dada_buck_destroy_voters(st);
	return ret;
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
	dada_buck_destroy_voters(st);
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

MODULE_DESCRIPTION("Xiaomi Dada MCA buck charging strategy/interface");
MODULE_LICENSE("GPL v2");
