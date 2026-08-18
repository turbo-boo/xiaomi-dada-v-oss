// SPDX-License-Identifier: GPL-2.0
/* Read-only charger userspace ABI for the Dada MCA bring-up phase. */
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

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "business_charger"
#endif

struct dada_charger_ro {
	struct device *sysfs_dev;
	struct notifier_block connect_nb;
	struct notifier_block type_nb;
};

enum dada_charger_ro_attr {
	DADA_CHARGER_RO_ONLINE = 0,
	DADA_CHARGER_RO_TYPE,
	DADA_CHARGER_RO_MAX_POWER,
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
	return protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PD, power);
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
		ret = platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER,
							      &value);
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
	mca_sysfs_attr_ro(dada_charger_ro, 0444, DADA_CHARGER_RO_MAX_POWER,
			  adapter_power),
};

static struct attribute *dada_charger_ro_attrs[
	ARRAY_SIZE(dada_charger_ro_info) + 1];
static const struct attribute_group dada_charger_ro_group = {
	.attrs = dada_charger_ro_attrs,
};

static int dada_charger_ro_event(struct notifier_block *nb,
				 unsigned long event, void *data)
{
	struct dada_charger_ro *chg;

	chg = container_of(nb, struct dada_charger_ro, connect_nb);
	if (chg->sysfs_dev)
		kobject_uevent(&chg->sysfs_dev->kobj, KOBJ_CHANGE);
	return NOTIFY_OK;
}

static int dada_charger_ro_type_event(struct notifier_block *nb,
				      unsigned long event, void *data)
{
	struct dada_charger_ro *chg;

	chg = container_of(nb, struct dada_charger_ro, type_nb);
	if (chg->sysfs_dev)
		kobject_uevent(&chg->sysfs_dev->kobj, KOBJ_CHANGE);
	return NOTIFY_OK;
}

static int dada_charger_ro_probe(struct platform_device *pdev)
{
	struct dada_charger_ro *chg;

	chg = devm_kzalloc(&pdev->dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;
	platform_set_drvdata(pdev, chg);

	mca_sysfs_init_attrs(dada_charger_ro_attrs, dada_charger_ro_info,
			     ARRAY_SIZE(dada_charger_ro_info));
	chg->sysfs_dev = mca_sysfs_create_group("xm_power", SYSFS_DEV_1,
						&dada_charger_ro_group);
	if (!chg->sysfs_dev)
		return -ENODEV;

	chg->connect_nb.notifier_call = dada_charger_ro_event;
	chg->type_nb.notifier_call = dada_charger_ro_type_event;
	mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
					&chg->connect_nb);
	mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGE_TYPE,
					&chg->type_nb);
	mca_log_info("read-only charger business ABI registered\n");
	return 0;
}

static int dada_charger_ro_remove(struct platform_device *pdev)
{
	struct dada_charger_ro *chg = platform_get_drvdata(pdev);

	if (!chg)
		return 0;
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGE_TYPE,
					  &chg->type_nb);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
					  &chg->connect_nb);
	mca_sysfs_remove_group("xm_power", chg->sysfs_dev,
			       &dada_charger_ro_group);
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

MODULE_DESCRIPTION("Xiaomi Dada MCA read-only charger business layer");
MODULE_LICENSE("GPL v2");
