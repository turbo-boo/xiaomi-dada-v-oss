// SPDX-License-Identifier: GPL-2.0
/*
 * Dada MCA wired charger business layer.
 *
 * Keep the standard Qualcomm USB power_supply as the transport-facing ABI and
 * expose Xiaomi MCA charger state/control under /sys/class/xm_power/charger.
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_bc12_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/protocol/protocol_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "business_charger"
#endif

struct dada_charger_business {
	struct device *dev;
	struct device *sysfs_dev;
	struct notifier_block charger_nb;
	struct notifier_block type_nb;
};

enum dada_charger_attr {
	DADA_CHARGER_ATTR_ONLINE = 0,
	DADA_CHARGER_ATTR_TYPE,
	DADA_CHARGER_ATTR_MAX_POWER,
	DADA_CHARGER_ATTR_INPUT_CURRENT_LIMIT,
	DADA_CHARGER_ATTR_TERM_CURRENT,
	DADA_CHARGER_ATTR_TERM_VOLTAGE,
};

static int dada_charger_get_type(int *type)
{
	int ret;

	if (!type)
		return -EINVAL;

	/* PPS/PD transport has precedence over legacy BC1.2 classification. */
	ret = protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PPS,
					      (unsigned int *)type);
	if (!ret && *type != XM_CHARGER_TYPE_UNKNOW)
		return 0;
	ret = protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD,
					      (unsigned int *)type);
	if (!ret && *type != XM_CHARGER_TYPE_UNKNOW)
		return 0;
	ret = protocol_class_get_adapter_type(ADAPTER_PROTOCOL_QC,
					      (unsigned int *)type);
	if (!ret && *type != XM_CHARGER_TYPE_UNKNOW)
		return 0;
	return platform_bc12_class_get_charge_type(BC12_MAIN_ROLE, type);
}

static int dada_charger_get_max_power(unsigned int *power)
{
	int ret;

	if (!power)
		return -EINVAL;
	ret = protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PPS, power);
	if (!ret && *power)
		return 0;
	ret = protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PD, power);
	if (!ret && *power)
		return 0;
	*power = 0;
	return ret;
}

static ssize_t dada_charger_sysfs_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *info;
	int value = 0;
	unsigned int power = 0;
	int ret = 0;

	info = container_of(attr, struct mca_sysfs_attr_info, attr);
	if (!info)
		return -EINVAL;

	switch (info->sysfs_attr_name) {
	case DADA_CHARGER_ATTR_ONLINE:
		ret = platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER,
							      &value);
		break;
	case DADA_CHARGER_ATTR_TYPE:
		ret = dada_charger_get_type(&value);
		break;
	case DADA_CHARGER_ATTR_MAX_POWER:
		ret = dada_charger_get_max_power(&power);
		if (!ret)
			value = power;
		break;
	case DADA_CHARGER_ATTR_INPUT_CURRENT_LIMIT:
		ret = platform_class_buckchg_ops_get_input_curr_lmt(
			MAIN_BUCK_CHARGER, &value);
		break;
	case DADA_CHARGER_ATTR_TERM_CURRENT:
		ret = platform_class_buckchg_ops_get_term_curr(MAIN_BUCK_CHARGER,
							 &value);
		break;
	case DADA_CHARGER_ATTR_TERM_VOLTAGE:
		ret = platform_class_buckchg_ops_get_term_volt(MAIN_BUCK_CHARGER,
							 &value);
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t dada_charger_sysfs_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *info;
	int value;
	int ret;

	info = container_of(attr, struct mca_sysfs_attr_info, attr);
	if (!info)
		return -EINVAL;
	ret = kstrtoint(buf, 0, &value);
	if (ret)
		return ret;
	if (value < 0)
		return -EINVAL;

	switch (info->sysfs_attr_name) {
	case DADA_CHARGER_ATTR_INPUT_CURRENT_LIMIT:
		ret = platform_class_buckchg_ops_set_input_curr_lmt(
			MAIN_BUCK_CHARGER, value);
		break;
	case DADA_CHARGER_ATTR_TERM_CURRENT:
		ret = platform_class_buckchg_ops_set_term_curr(MAIN_BUCK_CHARGER,
							 value);
		break;
	case DADA_CHARGER_ATTR_TERM_VOLTAGE:
		ret = platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER,
							 value);
		break;
	default:
		return -EPERM;
	}

	return ret ? ret : count;
}

static struct mca_sysfs_attr_info dada_charger_attrs_info[] = {
	mca_sysfs_attr_ro(dada_charger_sysfs, 0444,
			  DADA_CHARGER_ATTR_ONLINE, online),
	mca_sysfs_attr_ro(dada_charger_sysfs, 0444,
			  DADA_CHARGER_ATTR_TYPE, real_type),
	mca_sysfs_attr_ro(dada_charger_sysfs, 0444,
			  DADA_CHARGER_ATTR_MAX_POWER, adapter_power),
	mca_sysfs_attr_rw(dada_charger_sysfs, 0644,
			  DADA_CHARGER_ATTR_INPUT_CURRENT_LIMIT,
			  input_current_limit),
	mca_sysfs_attr_rw(dada_charger_sysfs, 0644,
			  DADA_CHARGER_ATTR_TERM_CURRENT, term_current),
	mca_sysfs_attr_rw(dada_charger_sysfs, 0644,
			  DADA_CHARGER_ATTR_TERM_VOLTAGE, term_voltage),
};

static struct attribute *dada_charger_attrs[
	ARRAY_SIZE(dada_charger_attrs_info) + 1];

static const struct attribute_group dada_charger_group = {
	.attrs = dada_charger_attrs,
};

static int dada_charger_event(struct notifier_block *nb,
			      unsigned long event, void *data)
{
	struct dada_charger_business *chg =
		container_of(nb, struct dada_charger_business, charger_nb);

	if (chg->sysfs_dev)
		kobject_uevent(&chg->sysfs_dev->kobj, KOBJ_CHANGE);
	return NOTIFY_OK;
}

static int dada_charger_type_event(struct notifier_block *nb,
				   unsigned long event, void *data)
{
	struct dada_charger_business *chg =
		container_of(nb, struct dada_charger_business, type_nb);

	if (chg->sysfs_dev)
		kobject_uevent(&chg->sysfs_dev->kobj, KOBJ_CHANGE);
	return NOTIFY_OK;
}

static int dada_charger_probe(struct platform_device *pdev)
{
	struct dada_charger_business *chg;
	int ret;

	chg = devm_kzalloc(&pdev->dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;
	chg->dev = &pdev->dev;
	platform_set_drvdata(pdev, chg);

	mca_sysfs_init_attrs(dada_charger_attrs, dada_charger_attrs_info,
			     ARRAY_SIZE(dada_charger_attrs_info));
	chg->sysfs_dev = mca_sysfs_create_group("xm_power", SYSFS_DEV_1,
						&dada_charger_group);
	if (!chg->sysfs_dev)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "failed to create charger sysfs group\n");

	chg->charger_nb.notifier_call = dada_charger_event;
	chg->type_nb.notifier_call = dada_charger_type_event;
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
					      &chg->charger_nb);
	if (ret)
		mca_log_info("charger notifier unavailable: %d\n", ret);
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGE_TYPE,
					      &chg->type_nb);
	if (ret)
		mca_log_info("type notifier unavailable: %d\n", ret);

	mca_log_info("charger business sysfs registered\n");
	return 0;
}

static int dada_charger_remove(struct platform_device *pdev)
{
	struct dada_charger_business *chg = platform_get_drvdata(pdev);

	if (!chg)
		return 0;
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGE_TYPE,
					  &chg->type_nb);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
					  &chg->charger_nb);
	mca_sysfs_remove_group("xm_power", chg->sysfs_dev,
			       &dada_charger_group);
	return 0;
}

static const struct of_device_id dada_charger_match[] = {
	{ .compatible = "mca,business_charger" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_charger_match);

static struct platform_driver dada_charger_driver = {
	.driver = {
		.name = "mca_business_charger",
		.of_match_table = dada_charger_match,
	},
	.probe = dada_charger_probe,
	.remove = dada_charger_remove,
};
module_platform_driver(dada_charger_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA wired charger business layer");
MODULE_LICENSE("GPL v2");
