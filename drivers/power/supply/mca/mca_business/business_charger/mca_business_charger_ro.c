// SPDX-License-Identifier: GPL-2.0
/*
 * Dada charger business compatibility layer.
 *
 * This keeps the conservative bring-up power path, while restoring the stock
 * charger_common userspace ABI used by Xiaomi's micharge service.
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_bc12_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/shared_memory/charger_partition_class.h>
#include <mca/strategy/strategy_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "business_charger"
#endif

extern int mca_wireless_rev_get_reverse_chg_state(int *state);

struct dada_charger_ro {
	struct device *sysfs_dev;
	struct device *dev;
	struct notifier_block connect_nb;
	struct notifier_block type_nb;
	int is_eu_model;
	int plate_shock;
	int handle_state;
	int stop_handle_charge;
	int last_handle_state;
	int last_stop_handle_charge;
	int allow_charge;
};

enum dada_charger_ro_attr {
	DADA_CHARGER_RO_ONLINE = 0,
	DADA_CHARGER_RO_TYPE,
	DADA_CHARGER_RO_MAX_POWER,
};

enum dada_charger_common_attr {
	CHG_COMMON_REAL_TYPE = 0,
	CHG_COMMON_POWER_MAX,
	CHG_COMMON_QUICK_CHARGE_TYPE,
	CHG_COMMON_IS_EU_MODEL,
	CHG_COMMON_PLATE_SHOCK,
	CHG_COMMON_REVERSE_QUICK_CHARGE,
	CHG_COMMON_HANDLE_STATE,
	CHG_COMMON_STOP_HANDLE_CHARGE,
	CHG_COMMON_WIRED_POWER_MAX,
};

static int dada_charger_ro_get_type(int *type)
{
	unsigned int adapter_type = XM_CHARGER_TYPE_UNKNOW;
	int ret;

	if (!type)
		return -EINVAL;
	ret = protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PPS, &adapter_type);
	if (!ret && adapter_type != XM_CHARGER_TYPE_UNKNOW)
		goto out;
	adapter_type = XM_CHARGER_TYPE_UNKNOW;
	ret = protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD, &adapter_type);
	if (!ret && adapter_type != XM_CHARGER_TYPE_UNKNOW)
		goto out;
	adapter_type = XM_CHARGER_TYPE_UNKNOW;
	ret = protocol_class_get_adapter_type(ADAPTER_PROTOCOL_QC, &adapter_type);
	if (!ret && adapter_type != XM_CHARGER_TYPE_UNKNOW)
		goto out;
	ret = platform_bc12_class_get_charge_type(BC12_MAIN_ROLE, type);
	return ret;
out:
	*type = adapter_type;
	return 0;
}

static int dada_charger_ro_get_power(unsigned int *power)
{
	int ret;

	if (!power)
		return -EINVAL;
	*power = 0;
	ret = protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PPS, power);
	if (!ret && *power)
		return 0;
	*power = 0;
	ret = protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PD, power);
	if (!ret && *power)
		return 0;
	*power = 0;
	return protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_QC, power);
}

static int dada_charger_quick_icon(int type, unsigned int power)
{
	switch (type) {
	case XM_CHARGER_TYPE_SDP:
	case XM_CHARGER_TYPE_CDP:
	case XM_CHARGER_TYPE_DCP:
	case XM_CHARGER_TYPE_FLOAT:
	case XM_CHARGER_TYPE_TYPEC:
	case XM_CHARGER_TYPE_ACA:
	case XM_CHARGER_TYPE_OCP:
		return ADP_ICON_TYPE_NORMAL;
	case XM_CHARGER_TYPE_PD:
	case XM_CHARGER_TYPE_HVDCP2:
	case XM_CHARGER_TYPE_HVDCP3:
		return ADP_ICON_TYPE_FAST;
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
		return ADP_ICON_TYPE_FLASH;
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		if (power >= 50)
			return ADP_ICON_TYPE_SUPER;
		if (power >= 30)
			return ADP_ICON_TYPE_TURBO;
		if (power >= 20)
			return ADP_ICON_TYPE_FLASH;
		return ADP_ICON_TYPE_FAST;
	default:
		return ADP_ICON_TYPE_NORMAL;
	}
}

static void dada_charger_handle_logic(struct dada_charger_ro *chg)
{
	int stop = chg->stop_handle_charge;
	int hs = chg->handle_state;
	int allow = -1;

	if (chg->last_stop_handle_charge != 0 && stop == 0 && hs != 0)
		allow = -1;
	else if (chg->last_stop_handle_charge != 0 && stop == 0 && hs == 0)
		allow = 1;
	else if (stop != 0 && chg->last_stop_handle_charge != stop)
		allow = 1;

	if (hs == 0 && chg->last_handle_state != 0)
		allow = 1;
	if (stop == 0 && hs != 0 && chg->last_handle_state != hs)
		allow = 0;

	chg->last_handle_state = hs;
	chg->last_stop_handle_charge = stop;
	if (allow < 0 || allow == chg->allow_charge)
		return;

	chg->allow_charge = allow;
	(void)mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					MCA_EVENT_HANDLE_ALLOW_CHARGE, allow);
	(void)mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					MCA_EVENT_HANDLE_ALLOW_CHARGE, allow);
}

static ssize_t dada_charger_ro_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *info =
		container_of(attr, struct mca_sysfs_attr_info, attr);
	int value = 0;
	unsigned int power = 0;
	int ret;

	switch (info->sysfs_attr_name) {
	case DADA_CHARGER_RO_ONLINE:
		ret = platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &value);
		break;
	case DADA_CHARGER_RO_TYPE:
		ret = dada_charger_ro_get_type(&value);
		break;
	case DADA_CHARGER_RO_MAX_POWER:
		ret = dada_charger_ro_get_power(&power);
		value = power;
		break;
	default:
		return -EINVAL;
	}
	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", value);
}

static struct mca_sysfs_attr_info dada_charger_ro_info[] = {
	mca_sysfs_attr_ro(dada_charger_ro, 0444, DADA_CHARGER_RO_ONLINE, online),
	mca_sysfs_attr_ro(dada_charger_ro, 0444, DADA_CHARGER_RO_TYPE, real_type),
	mca_sysfs_attr_ro(dada_charger_ro, 0444, DADA_CHARGER_RO_MAX_POWER, adapter_power),
};
static struct attribute *dada_charger_ro_attrs[ARRAY_SIZE(dada_charger_ro_info) + 1];
static const struct attribute_group dada_charger_ro_group = {
	.attrs = dada_charger_ro_attrs,
};

static ssize_t dada_charger_common_show(struct device *dev,
					struct device_attribute *attr, char *buf);
static ssize_t dada_charger_common_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count);

static struct mca_sysfs_attr_info dada_charger_common_info[] = {
	mca_sysfs_attr_ro(dada_charger_common, 0444, CHG_COMMON_REAL_TYPE, real_type),
	mca_sysfs_attr_ro(dada_charger_common, 0444, CHG_COMMON_POWER_MAX, power_max),
	mca_sysfs_attr_ro(dada_charger_common, 0444, CHG_COMMON_QUICK_CHARGE_TYPE, quick_charge_type),
	mca_sysfs_attr_rw(dada_charger_common, 0664, CHG_COMMON_IS_EU_MODEL, is_eu_model),
	mca_sysfs_attr_rw(dada_charger_common, 0664, CHG_COMMON_PLATE_SHOCK, plate_shock),
	mca_sysfs_attr_ro(dada_charger_common, 0444, CHG_COMMON_REVERSE_QUICK_CHARGE, reverse_quick_charge),
	mca_sysfs_attr_rw(dada_charger_common, 0664, CHG_COMMON_HANDLE_STATE, handle_state),
	mca_sysfs_attr_rw(dada_charger_common, 0664, CHG_COMMON_STOP_HANDLE_CHARGE, stop_handle_charge),
	mca_sysfs_attr_ro(dada_charger_common, 0444, CHG_COMMON_WIRED_POWER_MAX, wired_power_max),
};
static struct attribute *dada_charger_common_attrs[ARRAY_SIZE(dada_charger_common_info) + 1];
static const struct attribute_group dada_charger_common_group = {
	.attrs = dada_charger_common_attrs,
};

static struct dada_charger_ro *dada_charger_common_from_attr(
	struct device *dev, struct device_attribute *attr,
	struct mca_sysfs_attr_info **field)
{
	*field = mca_sysfs_lookup_attr(attr->attr.name, dada_charger_common_info,
				       ARRAY_SIZE(dada_charger_common_info));
	return *field ? dev_get_drvdata(dev) : NULL;
}

static ssize_t dada_charger_common_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *field;
	struct dada_charger_ro *chg = dada_charger_common_from_attr(dev, attr, &field);
	unsigned int power = 0;
	int type = XM_CHARGER_TYPE_UNKNOW;
	int value = 0;
	bool eu = false;

	if (!chg)
		return -ENODEV;
	(void)dada_charger_ro_get_type(&type);
	(void)dada_charger_ro_get_power(&power);

	switch (field->sysfs_attr_name) {
	case CHG_COMMON_REAL_TYPE:
		value = type;
		break;
	case CHG_COMMON_POWER_MAX:
	case CHG_COMMON_WIRED_POWER_MAX:
		value = power;
		break;
	case CHG_COMMON_QUICK_CHARGE_TYPE:
		value = dada_charger_quick_icon(type, power);
		break;
	case CHG_COMMON_IS_EU_MODEL:
		if (!charger_partition_get_eu_model(&eu))
			chg->is_eu_model = eu;
		value = chg->is_eu_model;
		break;
	case CHG_COMMON_PLATE_SHOCK:
		value = chg->plate_shock;
		break;
	case CHG_COMMON_REVERSE_QUICK_CHARGE:
		if (mca_wireless_rev_get_reverse_chg_state(&value))
			value = 0;
		break;
	case CHG_COMMON_HANDLE_STATE:
		value = chg->handle_state;
		break;
	case CHG_COMMON_STOP_HANDLE_CHARGE:
		value = chg->stop_handle_charge;
		break;
	default:
		return -EINVAL;
	}
	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t dada_charger_common_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *field;
	struct dada_charger_ro *chg = dada_charger_common_from_attr(dev, attr, &field);
	int value;

	if (!chg || kstrtoint(buf, 0, &value))
		return -EINVAL;
	switch (field->sysfs_attr_name) {
	case CHG_COMMON_IS_EU_MODEL:
		chg->is_eu_model = !!value;
		(void)platform_class_buckchg_ops_set_eu_model(MAIN_BUCK_CHARGER,
							       !!value);
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_IS_EU_MODEL,
				       &chg->is_eu_model);
		break;
	case CHG_COMMON_PLATE_SHOCK:
		chg->plate_shock = value;
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_PLATE_SHOCK,
				       &chg->plate_shock);
		break;
	case CHG_COMMON_HANDLE_STATE:
		chg->handle_state = value;
		dada_charger_handle_logic(chg);
		break;
	case CHG_COMMON_STOP_HANDLE_CHARGE:
		chg->stop_handle_charge = value;
		dada_charger_handle_logic(chg);
		break;
	default:
		return -EACCES;
	}
	return count;
}

static int dada_charger_ro_event(struct notifier_block *nb,
				 unsigned long event, void *data)
{
	struct dada_charger_ro *chg = container_of(nb, struct dada_charger_ro, connect_nb);

	if (chg->sysfs_dev)
		kobject_uevent(&chg->sysfs_dev->kobj, KOBJ_CHANGE);
	return NOTIFY_OK;
}

static int dada_charger_ro_type_event(struct notifier_block *nb,
				      unsigned long event, void *data)
{
	struct dada_charger_ro *chg = container_of(nb, struct dada_charger_ro, type_nb);

	if (chg->sysfs_dev)
		kobject_uevent(&chg->sysfs_dev->kobj, KOBJ_CHANGE);
	return NOTIFY_OK;
}

static int dada_charger_ro_probe(struct platform_device *pdev)
{
	struct dada_charger_ro *chg;
	bool eu = false;
	int ret;

	chg = devm_kzalloc(&pdev->dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;
	chg->dev = &pdev->dev;
	chg->allow_charge = 1;
	platform_set_drvdata(pdev, chg);

	mca_sysfs_init_attrs(dada_charger_ro_attrs, dada_charger_ro_info,
			     ARRAY_SIZE(dada_charger_ro_info));
	chg->sysfs_dev = mca_sysfs_create_group("xm_power", SYSFS_DEV_1,
						&dada_charger_ro_group);
	if (!chg->sysfs_dev)
		return -ENODEV;

	mca_sysfs_init_attrs(dada_charger_common_attrs, dada_charger_common_info,
			     ARRAY_SIZE(dada_charger_common_info));
	ret = mca_sysfs_create_link_group(SYSFS_DEV_1, "charger_common",
					  &pdev->dev, &dada_charger_common_group);
	if (ret) {
		mca_sysfs_remove_group("xm_power", chg->sysfs_dev,
				       &dada_charger_ro_group);
		return ret;
	}

	if (!charger_partition_get_eu_model(&eu))
		chg->is_eu_model = eu;

	chg->connect_nb.notifier_call = dada_charger_ro_event;
	chg->type_nb.notifier_call = dada_charger_ro_type_event;
	(void)mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
					      &chg->connect_nb);
	(void)mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGE_TYPE,
					      &chg->type_nb);
	mca_log_info("charger business + stock charger_common ABI registered\n");
	return 0;
}

static int dada_charger_ro_remove(struct platform_device *pdev)
{
	struct dada_charger_ro *chg = platform_get_drvdata(pdev);

	if (!chg)
		return 0;
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGE_TYPE,
						&chg->type_nb);
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
						&chg->connect_nb);
	mca_sysfs_remove_link_group(SYSFS_DEV_1, "charger_common", &pdev->dev,
				    &dada_charger_common_group);
	mca_sysfs_remove_group("xm_power", chg->sysfs_dev, &dada_charger_ro_group);
	return 0;
}

static const struct of_device_id dada_charger_ro_match[] = {
	{ .compatible = "mca,business_charger" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_charger_ro_match);

static struct platform_driver dada_charger_ro_driver = {
	.driver = {
		.name = "mca_business_charger",
		.of_match_table = dada_charger_ro_match,
	},
	.probe = dada_charger_ro_probe,
	.remove = dada_charger_ro_remove,
};
module_platform_driver(dada_charger_ro_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA charger business compatibility layer");
MODULE_LICENSE("GPL v2");
