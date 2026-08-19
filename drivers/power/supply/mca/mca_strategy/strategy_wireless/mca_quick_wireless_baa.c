// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi MCA quick-wireless BAA/status baseline for Dada.
 *
 * This restores the stock WL_QC smart-charge data sink and strategy ABI but
 * intentionally does not start charge pumps or raise wireless voltage/current.
 * The stored BAA tables become the policy input for the later SC8585-validated
 * quick-wireless state machine.
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/smartchg/smart_chg_class.h>
#include <mca/strategy/strategy_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_quick_wireless"
#endif

#define DADA_WLS_BAA_MAX_RECORDS 16
#define DADA_WLS_BAA_MAX_STEPS   32

struct dada_wls_baa_entry {
	u32 type;
	u32 ffc;
	int temp_idx;
	int temp_min;
	int temp_max;
	u32 step_count;
	struct smart_batt_spec_curve *steps;
};

struct dada_quick_wireless {
	struct device *dev;
	struct mutex lock;
	struct notifier_block connect_nb;
	struct dada_wls_baa_entry *ffc;
	struct dada_wls_baa_entry *normal;
	u32 ffc_count;
	u32 normal_count;
	bool online;
	bool baa_ready;
	bool soc_limit;
	bool pwr_boost;
	bool force_stop;
	int delta_fv;
	int delta_ichg;
	int fcc_limit;
	int qc_status;
	int adapter_type;
	int max_power;
};

static void dada_wls_baa_free(struct dada_wls_baa_entry *table, u32 count)
{
	u32 i;

	if (!table)
		return;
	for (i = 0; i < count; i++)
		kfree(table[i].steps);
	kfree(table);
}

static int dada_wls_baa_parse(const char **cursor, u32 count,
			      struct dada_wls_baa_entry **out)
{
	struct dada_wls_baa_entry *table;
	u32 i;

	if (!cursor || !*cursor || !out)
		return -EINVAL;
	if (count > DADA_WLS_BAA_MAX_RECORDS)
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

		if (spec->step_size > DADA_WLS_BAA_MAX_STEPS ||
		    spec->t_range.idx < 0 ||
		    spec->t_range.min >= spec->t_range.max) {
			dada_wls_baa_free(table, count);
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
				dada_wls_baa_free(table, count);
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

static int dada_quick_wls_update_baa(void *data, char *baa_para,
				     int ffc_size, int normal_size)
{
	struct dada_quick_wireless *info = data;
	struct dada_wls_baa_entry *ffc = NULL, *normal = NULL;
	const char *cursor = baa_para;
	int ret;

	if (!info || !baa_para || ffc_size < 0 || normal_size < 0)
		return -EINVAL;
	if (ffc_size > DADA_WLS_BAA_MAX_RECORDS ||
	    normal_size > DADA_WLS_BAA_MAX_RECORDS)
		return -E2BIG;

	ret = dada_wls_baa_parse(&cursor, ffc_size, &ffc);
	if (ret)
		return ret;
	ret = dada_wls_baa_parse(&cursor, normal_size, &normal);
	if (ret) {
		dada_wls_baa_free(ffc, ffc_size);
		return ret;
	}

	mutex_lock(&info->lock);
	dada_wls_baa_free(info->ffc, info->ffc_count);
	dada_wls_baa_free(info->normal, info->normal_count);
	info->ffc = ffc;
	info->normal = normal;
	info->ffc_count = ffc_size;
	info->normal_count = normal_size;
	info->baa_ready = true;
	mutex_unlock(&info->lock);

	mca_log_info("wireless BAA updated ffc=%d normal=%d\n",
		     ffc_size, normal_size);
	return 0;
}

static int dada_quick_wls_set_delta_fv(void *data, int value)
{
	struct dada_quick_wireless *info = data;

	if (!info)
		return -EINVAL;
	mutex_lock(&info->lock);
	info->delta_fv = value;
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_quick_wls_set_delta_ichg(void *data, int value)
{
	struct dada_quick_wireless *info = data;

	if (!info)
		return -EINVAL;
	mutex_lock(&info->lock);
	info->delta_ichg = value;
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_quick_wls_set_fcc(void *data, int value)
{
	struct dada_quick_wireless *info = data;

	if (!info)
		return -EINVAL;
	mutex_lock(&info->lock);
	info->fcc_limit = value;
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_quick_wls_set_boost(void *data, int enable)
{
	struct dada_quick_wireless *info = data;

	if (!info)
		return -EINVAL;
	mutex_lock(&info->lock);
	info->pwr_boost = !!enable;
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_quick_wls_set_soc_limit(void *data, int enable)
{
	struct dada_quick_wireless *info = data;

	if (!info)
		return -EINVAL;
	mutex_lock(&info->lock);
	info->soc_limit = !!enable;
	mutex_unlock(&info->lock);
	return 0;
}

static void dada_quick_wls_refresh(struct dada_quick_wireless *info)
{
	u8 max_power = 0;
	int present = 0;
	int adapter = ADAPTER_NONE;

	if (!platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &present))
		info->online = !!present;
	if (!info->online) {
		info->adapter_type = ADAPTER_NONE;
		info->max_power = 0;
		info->qc_status = MCA_QUICK_CHG_STS_NO_CHARGING;
		return;
	}
	if (!platform_class_wireless_get_tx_adapter_by_i2c(WIRELESS_ROLE_MASTER,
							    &adapter))
		info->adapter_type = adapter;
	if (!platform_class_wireless_get_tx_max_power(WIRELESS_ROLE_MASTER,
						       &max_power))
		info->max_power = max_power;
}

static int dada_quick_wls_process(int event, int value, void *data)
{
	struct dada_quick_wireless *info = data;

	if (!info)
		return -EINVAL;
	mutex_lock(&info->lock);
	switch (event) {
	case MCA_EVENT_WIRELESS_CONNECT:
		info->online = true;
		info->force_stop = false;
		dada_quick_wls_refresh(info);
		break;
	case MCA_EVENT_WIRELESS_DISCONNECT:
		info->online = false;
		info->force_stop = true;
		dada_quick_wls_refresh(info);
		break;
	case MCA_EVENT_WIRELESS_INT_CHANGE:
		dada_quick_wls_refresh(info);
		break;
	case MCA_EVENT_CHARGE_ACTION:
		/* State is observed but no CP/VOUT/FCC transition is started yet. */
		info->force_stop = !value;
		break;
	case MCA_EVENT_WIRELESS_AUDIO_PHONE_STS:
		break;
	default:
		mutex_unlock(&info->lock);
		return -EOPNOTSUPP;
	}
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_quick_wls_get_status(int status, void *value, void *data)
{
	struct dada_quick_wireless *info = data;

	if (!info || !value)
		return -EINVAL;
	mutex_lock(&info->lock);
	switch (status) {
	case STRATEGY_STATUS_TYPE_ONLINE:
		*(bool *)value = info->online;
		break;
	case STRATEGY_STATUS_TYPE_QC_ENABLE:
		*(bool *)value = info->online && !info->force_stop &&
				 info->adapter_type >= ADAPTER_XIAOMI_QC3;
		break;
	case STRATEGY_STATUS_TYPE_QC_START_FLAG:
		*(bool *)value = false;
		break;
	case STRATEGY_STATUS_TYPE_QC_CHARGE_STS:
		*(int *)value = info->qc_status;
		break;
	case STRATEGY_STATUS_TYPE_QC_TYPE:
		if (info->adapter_type >= ADAPTER_XIAOMI_PD_40W)
			*(int *)value = ADP_ICON_TYPE_SUPER;
		else if (info->adapter_type >= ADAPTER_XIAOMI_QC3)
			*(int *)value = ADP_ICON_TYPE_FLASH;
		else
			*(int *)value = ADP_ICON_TYPE_NORMAL;
		break;
	case STRATEGY_STATUS_TYPE_QC_IBAT_MAX:
		*(int *)value = info->fcc_limit;
		break;
	case STRATEGY_STATUS_TYPE_POWER_MAX:
	case STRATEGY_STATUS_TYPE_QC_MAX_POWER:
		*(int *)value = info->max_power;
		break;
	default:
		mutex_unlock(&info->lock);
		return -EOPNOTSUPP;
	}
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_quick_wls_connect_event(struct notifier_block *nb,
					unsigned long event, void *data)
{
	struct dada_quick_wireless *info =
		container_of(nb, struct dada_quick_wireless, connect_nb);

	if (event == MCA_EVENT_WIRELESS_CONNECT ||
	    event == MCA_EVENT_WIRELESS_DISCONNECT)
		(void)dada_quick_wls_process(event,
				event == MCA_EVENT_WIRELESS_CONNECT, info);
	return NOTIFY_OK;
}

static struct mca_smartchg_if_ops dada_quick_wls_smart_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_WL_QC,
	.set_delta_fv = dada_quick_wls_set_delta_fv,
	.set_delta_ichg = dada_quick_wls_set_delta_ichg,
	.set_fcc = dada_quick_wls_set_fcc,
	.set_pwr_boost_sts = dada_quick_wls_set_boost,
	.set_soc_limit_sts = dada_quick_wls_set_soc_limit,
	.update_baa_para = dada_quick_wls_update_baa,
};

static int dada_quick_wls_probe(struct platform_device *pdev)
{
	struct dada_quick_wireless *info;
	int present = 0;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	info->qc_status = MCA_QUICK_CHG_STS_NO_CHARGING;
	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);

	ret = mca_strategy_ops_register(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
					dada_quick_wls_process,
					dada_quick_wls_get_status, NULL, info);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register quick wireless strategy\n");

	dada_quick_wls_smart_ops.data = info;
	ret = mca_smartchg_if_ops_register(&dada_quick_wls_smart_ops);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register WL_QC smart-charge ops\n");

	info->connect_nb.notifier_call = dada_quick_wls_connect_event;
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
					      &info->connect_nb);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register wireless notifier\n");

	if (!platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &present) &&
	    present)
		(void)dada_quick_wls_process(MCA_EVENT_WIRELESS_CONNECT, 1, info);
	mca_log_info("quick wireless BAA/status ABI ready online=%d\n",
		     info->online);
	return 0;
}

static int dada_quick_wls_remove(struct platform_device *pdev)
{
	struct dada_quick_wireless *info = platform_get_drvdata(pdev);

	if (!info)
		return 0;
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
					  &info->connect_nb);
	mutex_lock(&info->lock);
	dada_wls_baa_free(info->ffc, info->ffc_count);
	dada_wls_baa_free(info->normal, info->normal_count);
	info->ffc = NULL;
	info->normal = NULL;
	info->ffc_count = 0;
	info->normal_count = 0;
	mutex_unlock(&info->lock);
	mutex_destroy(&info->lock);
	return 0;
}

static const struct of_device_id dada_quick_wls_match[] = {
	{ .compatible = "mca,quick_wireless" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_quick_wls_match);

static struct platform_driver dada_quick_wls_driver = {
	.driver = {
		.name = "mca_quick_wireless",
		.of_match_table = dada_quick_wls_match,
	},
	.probe = dada_quick_wls_probe,
	.remove = dada_quick_wls_remove,
};
module_platform_driver(dada_quick_wls_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA quick wireless BAA/status baseline");
MODULE_LICENSE("GPL v2");
