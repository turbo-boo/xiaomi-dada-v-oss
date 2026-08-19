// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/protocol/protocol_class.h>
#include <mca/smartchg/smart_chg_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_quick_charge"
#endif

#define MCA_QUICK_BAA_MAX_RECORDS 16
#define MCA_QUICK_BAA_MAX_STEPS 32

struct dada_baa_entry {
	u32 type;
	u32 ffc;
	int temp_idx;
	int temp_min;
	int temp_max;
	u32 step_count;
	struct smart_batt_spec_curve *steps;
};

struct dada_quickchg {
	struct device *dev;
	struct mutex lock;
	struct notifier_block connect_nb;
	struct notifier_block type_nb;
	struct dada_baa_entry *ffc;
	struct dada_baa_entry *normal;
	u32 ffc_count;
	u32 normal_count;
	int delta_fv;
	int delta_ichg;
	int fcc_limit;
	bool online;
	bool pwr_boost;
	bool soc_limit;
	bool baa_ready;
};

static void dada_baa_free_table(struct dada_baa_entry *table, u32 count)
{
	u32 i;

	if (!table)
		return;
	for (i = 0; i < count; i++)
		kfree(table[i].steps);
	kfree(table);
}

static int dada_baa_parse_table(const char **cursor, u32 count,
				struct dada_baa_entry **out)
{
	struct dada_baa_entry *table;
	u32 i;

	if (!cursor || !*cursor || !out)
		return -EINVAL;
	if (count > MCA_QUICK_BAA_MAX_RECORDS)
		return -E2BIG;
	if (!count) {
		*out = NULL;
		return 0;
	}

	table = kcalloc(count, sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		const struct smart_batt_spec *spec =
			(const struct smart_batt_spec *)*cursor;
		size_t bytes;

		if (spec->step_size > MCA_QUICK_BAA_MAX_STEPS ||
		    spec->t_range.min >= spec->t_range.max ||
		    spec->t_range.idx < 0) {
			dada_baa_free_table(table, count);
			return -EINVAL;
		}

		table[i].type = spec->type;
		table[i].ffc = spec->ffc;
		table[i].temp_idx = spec->t_range.idx;
		table[i].temp_min = spec->t_range.min;
		table[i].temp_max = spec->t_range.max;
		table[i].step_count = spec->step_size;
		if (spec->step_size) {
			table[i].steps = kmemdup(spec->steps,
						 sizeof(*spec->steps) * spec->step_size,
						 GFP_KERNEL);
			if (!table[i].steps) {
				dada_baa_free_table(table, count);
				return -ENOMEM;
			}
		}

		bytes = offsetof(struct smart_batt_spec, steps) +
			(size_t)spec->step_size * sizeof(*spec->steps);
		*cursor += bytes;
	}

	*out = table;
	return 0;
}

static int dada_quick_update_baa(void *data, char *baa_para,
				 int ffc_size, int normal_size)
{
	struct dada_quickchg *info = data;
	struct dada_baa_entry *ffc = NULL, *normal = NULL;
	const char *cursor = baa_para;
	int ret;

	if (!info || !baa_para || ffc_size < 0 || normal_size < 0)
		return -EINVAL;
	if (ffc_size > MCA_QUICK_BAA_MAX_RECORDS ||
	    normal_size > MCA_QUICK_BAA_MAX_RECORDS)
		return -E2BIG;

	ret = dada_baa_parse_table(&cursor, ffc_size, &ffc);
	if (ret)
		return ret;
	ret = dada_baa_parse_table(&cursor, normal_size, &normal);
	if (ret) {
		dada_baa_free_table(ffc, ffc_size);
		return ret;
	}

	mutex_lock(&info->lock);
	dada_baa_free_table(info->ffc, info->ffc_count);
	dada_baa_free_table(info->normal, info->normal_count);
	info->ffc = ffc;
	info->normal = normal;
	info->ffc_count = ffc_size;
	info->normal_count = normal_size;
	info->baa_ready = true;
	mutex_unlock(&info->lock);

	mca_log_info("wired BAA updated ffc=%d normal=%d\n",
		     ffc_size, normal_size);
	return 0;
}

static int dada_quick_set_delta_fv(void *data, int value)
{
	struct dada_quickchg *info = data;

	if (!info)
		return -EINVAL;
	mutex_lock(&info->lock);
	info->delta_fv = value;
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_quick_set_delta_ichg(void *data, int value)
{
	struct dada_quickchg *info = data;

	if (!info)
		return -EINVAL;
	mutex_lock(&info->lock);
	info->delta_ichg = value;
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_quick_set_fcc(void *data, int value)
{
	struct dada_quickchg *info = data;

	if (!info)
		return -EINVAL;
	mutex_lock(&info->lock);
	info->fcc_limit = value;
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_quick_set_power_boost(void *data, int enable)
{
	struct dada_quickchg *info = data;

	if (!info)
		return -EINVAL;
	mutex_lock(&info->lock);
	info->pwr_boost = !!enable;
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_quick_set_soc_limit(void *data, int enable)
{
	struct dada_quickchg *info = data;

	if (!info)
		return -EINVAL;
	mutex_lock(&info->lock);
	info->soc_limit = !!enable;
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_quick_adapter_type(void)
{
	unsigned int type = XM_CHARGER_TYPE_UNKNOW;

	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PPS, &type) &&
	    type != XM_CHARGER_TYPE_UNKNOW)
		return type;
	type = XM_CHARGER_TYPE_UNKNOW;
	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD, &type) &&
	    type != XM_CHARGER_TYPE_UNKNOW)
		return type;
	type = XM_CHARGER_TYPE_UNKNOW;
	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_QC, &type))
		return type;
	return XM_CHARGER_TYPE_UNKNOW;
}

static unsigned int dada_quick_adapter_power(void)
{
	unsigned int power = 0;

	if (!protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PPS, &power) &&
	    power)
		return power;
	power = 0;
	if (!protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PD, &power))
		return power;
	return 0;
}

static int dada_quick_process(int event, int value, void *data)
{
	struct dada_quickchg *info = data;

	if (!info)
		return -EINVAL;
	switch (event) {
	case MCA_EVENT_USB_CONNECT:
		info->online = true;
		break;
	case MCA_EVENT_USB_DISCONNECT:
		info->online = false;
		break;
	case MCA_EVENT_PPS_PTF:
		/* Preserve event reception; no automatic PD/CP control in bring-up. */
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int dada_quick_get_status(int status, void *value, void *data)
{
	struct dada_quickchg *info = data;

	if (!info || !value)
		return -EINVAL;

	mutex_lock(&info->lock);
	switch (status) {
	case STRATEGY_STATUS_TYPE_ONLINE:
		*(bool *)value = info->online;
		break;
	case STRATEGY_STATUS_TYPE_QC_ENABLE:
	case STRATEGY_STATUS_TYPE_QC_START_FLAG:
		*(bool *)value = false;
		break;
	case STRATEGY_STATUS_TYPE_QC_CHARGE_STS:
		*(int *)value = MCA_QUICK_CHG_STS_NO_CHARGING;
		break;
	case STRATEGY_STATUS_TYPE_QC_TYPE:
		*(int *)value = dada_quick_adapter_type();
		break;
	case STRATEGY_STATUS_TYPE_QC_IBAT_MAX:
		*(int *)value = info->fcc_limit;
		break;
	case STRATEGY_STATUS_TYPE_POWER_MAX:
	case STRATEGY_STATUS_TYPE_QC_MAX_POWER:
		*(unsigned int *)value = dada_quick_adapter_power();
		break;
	default:
		mutex_unlock(&info->lock);
		return -EOPNOTSUPP;
	}
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_quick_connect_event(struct notifier_block *nb,
				    unsigned long event, void *data)
{
	struct dada_quickchg *info =
		container_of(nb, struct dada_quickchg, connect_nb);

	if (event == MCA_EVENT_USB_CONNECT)
		info->online = true;
	else if (event == MCA_EVENT_USB_DISCONNECT)
		info->online = false;
	return NOTIFY_OK;
}

static int dada_quick_type_event(struct notifier_block *nb,
				 unsigned long event, void *data)
{
	return NOTIFY_OK;
}

static struct mca_smartchg_if_ops dada_quick_smartchg_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_QC,
	.set_delta_fv = dada_quick_set_delta_fv,
	.set_delta_ichg = dada_quick_set_delta_ichg,
	.set_fcc = dada_quick_set_fcc,
	.set_pwr_boost_sts = dada_quick_set_power_boost,
	.set_soc_limit_sts = dada_quick_set_soc_limit,
	.update_baa_para = dada_quick_update_baa,
};

static int dada_quick_probe(struct platform_device *pdev)
{
	struct dada_quickchg *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);

	ret = mca_strategy_ops_register(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					dada_quick_process,
					dada_quick_get_status, NULL, info);
	if (ret)
		return ret;

	dada_quick_smartchg_ops.data = info;
	ret = mca_smartchg_if_ops_register(&dada_quick_smartchg_ops);
	if (ret)
		return ret;

	info->connect_nb.notifier_call = dada_quick_connect_event;
	info->type_nb.notifier_call = dada_quick_type_event;
	mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
					&info->connect_nb);
	mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGE_TYPE,
					&info->type_nb);

	mca_log_info("quick-charge BAA/status consumer registered\n");
	return 0;
}

static int dada_quick_remove(struct platform_device *pdev)
{
	struct dada_quickchg *info = platform_get_drvdata(pdev);

	if (!info)
		return 0;
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGE_TYPE,
					  &info->type_nb);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
					  &info->connect_nb);
	mutex_lock(&info->lock);
	dada_baa_free_table(info->ffc, info->ffc_count);
	dada_baa_free_table(info->normal, info->normal_count);
	info->ffc = NULL;
	info->normal = NULL;
	info->ffc_count = 0;
	info->normal_count = 0;
	mutex_unlock(&info->lock);
	return 0;
}

static const struct of_device_id dada_quick_match[] = {
	{ .compatible = "mca,quick_charger" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_quick_match);

static struct platform_driver dada_quick_driver = {
	.driver = {
		.name = "mca_quick_charger",
		.of_match_table = dada_quick_match,
	},
	.probe = dada_quick_probe,
	.remove = dada_quick_remove,
};
module_platform_driver(dada_quick_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA wired BAA quick-charge consumer");
MODULE_LICENSE("GPL v2");
