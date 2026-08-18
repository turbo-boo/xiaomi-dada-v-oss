// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/smartchg/smart_chg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_smart_charge"
#endif

enum smartchg_attr_id {
	MCA_PROP_SMARTCHG = 0,
	MCA_PROP_SMARTCHG_FV,
	MCA_PROP_SMARTCHG_ICHG,
	MCA_PROP_SMARTBATT,
	MCA_PROP_SMARTNIGHT,
	MCA_PROP_POSTURE,
	MCA_PROP_SCENE,
	MCA_PROP_BOARD_TEMP,
	MCA_PROP_SMART_SIC_MODE,
};

struct smart_charge_info {
	struct device *dev;
	struct device *sysfs_dev;
	struct mutex lock;
	int cell_type;
	int smart_chg;
	int delta_fv;
	int delta_ichg;
	int smart_batt;
	int smart_night;
	int posture;
	int scene;
	int board_temp;
	int smart_sic_mode;
	int limit_soc;
	bool extreme_cold_enabled;
};

static struct smart_charge_info *global_smartchg_info;
static struct mca_smartchg_if_ops *g_mca_smartchg_if_ops[MCA_SMARTCHG_IF_CHG_TYPE_END];
static DEFINE_MUTEX(mca_smartchg_ops_lock);

static ssize_t smart_charge_sysfs_show(struct device *dev,
				       struct device_attribute *attr, char *buf);
static ssize_t smart_charge_sysfs_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count);

static struct mca_sysfs_attr_info smartchg_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTCHG, smart_chg),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTCHG_FV, smart_fv),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTCHG_ICHG, smart_ichg),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTBATT, smart_batt),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTNIGHT, smart_night),
	mca_sysfs_attr_ro(smart_charge_sysfs, 0440, MCA_PROP_POSTURE, posture),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SCENE, scene),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_BOARD_TEMP, board_temp),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMART_SIC_MODE, smart_sic_mode),
};

#define SMARTCHG_SYSFS_ATTRS_SIZE ARRAY_SIZE(smartchg_sysfs_field_tbl)
static struct attribute *smartchg_sysfs_attrs[SMARTCHG_SYSFS_ATTRS_SIZE + 1];
static const struct attribute_group smartchg_sysfs_attr_group = {
	.attrs = smartchg_sysfs_attrs,
};

int mca_smartchg_if_ops_register(struct mca_smartchg_if_ops *ops)
{
	if (!ops || ops->type < 0 || ops->type >= MCA_SMARTCHG_IF_CHG_TYPE_END)
		return -EINVAL;
	mutex_lock(&mca_smartchg_ops_lock);
	g_mca_smartchg_if_ops[ops->type] = ops;
	mutex_unlock(&mca_smartchg_ops_lock);
	return 0;
}
EXPORT_SYMBOL(mca_smartchg_if_ops_register);

static void mca_smartchg_apply_delta_fv(int value)
{
	int i;

	mutex_lock(&mca_smartchg_ops_lock);
	for (i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
		struct mca_smartchg_if_ops *ops = g_mca_smartchg_if_ops[i];

		if (ops && ops->set_delta_fv)
			ops->set_delta_fv(ops->data, value);
	}
	mutex_unlock(&mca_smartchg_ops_lock);
}

static void mca_smartchg_apply_delta_ichg(int value)
{
	int i;

	mutex_lock(&mca_smartchg_ops_lock);
	for (i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
		struct mca_smartchg_if_ops *ops = g_mca_smartchg_if_ops[i];

		if (ops && ops->set_delta_ichg)
			ops->set_delta_ichg(ops->data, value);
	}
	mutex_unlock(&mca_smartchg_ops_lock);
}

void mca_smartchg_set_scene(int scene)
{
	struct smart_charge_info *info = global_smartchg_info;

	if (!info)
		return;
	mutex_lock(&info->lock);
	info->scene = scene;
	mutex_unlock(&info->lock);
}
EXPORT_SYMBOL(mca_smartchg_set_scene);

int mca_smartchg_get_scene(void)
{
	struct smart_charge_info *info = global_smartchg_info;
	int scene = 0;

	if (!info)
		return 0;
	mutex_lock(&info->lock);
	scene = info->scene;
	mutex_unlock(&info->lock);
	return scene;
}
EXPORT_SYMBOL(mca_smartchg_get_scene);

void mca_smartchg_set_board_temp(int board_temp)
{
	struct smart_charge_info *info = global_smartchg_info;

	if (!info)
		return;
	mutex_lock(&info->lock);
	info->board_temp = board_temp;
	mutex_unlock(&info->lock);
}
EXPORT_SYMBOL(mca_smartchg_set_board_temp);

int mca_smartchg_get_board_temp(void)
{
	struct smart_charge_info *info = global_smartchg_info;
	int value = 0;

	if (!info)
		return 0;
	mutex_lock(&info->lock);
	value = info->board_temp;
	mutex_unlock(&info->lock);
	return value;
}
EXPORT_SYMBOL(mca_smartchg_get_board_temp);

int mca_smartchg_is_extreme_cold_enabled(void)
{
	return global_smartchg_info ? global_smartchg_info->extreme_cold_enabled : 0;
}
EXPORT_SYMBOL(mca_smartchg_is_extreme_cold_enabled);

int mca_smartchg_get_limit_soc(void)
{
	return global_smartchg_info ? global_smartchg_info->limit_soc : 0;
}
EXPORT_SYMBOL(mca_smartchg_get_limit_soc);

static ssize_t smart_charge_sysfs_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	struct smart_charge_info *info = dev_get_drvdata(dev);
	int value = 0;

	if (!info)
		return -ENODEV;
	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  smartchg_sysfs_field_tbl,
					  SMARTCHG_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -EINVAL;

	mutex_lock(&info->lock);
	switch (attr_info->sysfs_attr_name) {
	case MCA_PROP_SMARTCHG:
		value = info->smart_chg;
		break;
	case MCA_PROP_SMARTCHG_FV:
		value = info->delta_fv;
		break;
	case MCA_PROP_SMARTCHG_ICHG:
		value = info->delta_ichg;
		break;
	case MCA_PROP_SMARTBATT:
		value = info->smart_batt;
		break;
	case MCA_PROP_SMARTNIGHT:
		value = info->smart_night;
		break;
	case MCA_PROP_POSTURE:
		value = info->posture;
		break;
	case MCA_PROP_SCENE:
		value = info->scene;
		break;
	case MCA_PROP_BOARD_TEMP:
		value = info->board_temp;
		break;
	case MCA_PROP_SMART_SIC_MODE:
		value = info->smart_sic_mode;
		break;
	default:
		mutex_unlock(&info->lock);
		return -EINVAL;
	}
	mutex_unlock(&info->lock);
	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t smart_charge_sysfs_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	struct smart_charge_info *info = dev_get_drvdata(dev);
	int value, apply_fv = 0, apply_ichg = 0;

	if (!info)
		return -ENODEV;
	if (kstrtoint(buf, 0, &value))
		return -EINVAL;
	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  smartchg_sysfs_field_tbl,
					  SMARTCHG_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -EINVAL;

	mutex_lock(&info->lock);
	switch (attr_info->sysfs_attr_name) {
	case MCA_PROP_SMARTCHG:
		info->smart_chg = value;
		break;
	case MCA_PROP_SMARTCHG_FV:
		info->delta_fv = value * max(info->cell_type, 1);
		apply_fv = 1;
		value = info->delta_fv;
		break;
	case MCA_PROP_SMARTCHG_ICHG:
		info->delta_ichg = value;
		apply_ichg = 1;
		break;
	case MCA_PROP_SMARTBATT:
		info->smart_batt = value;
		break;
	case MCA_PROP_SMARTNIGHT:
		info->smart_night = value;
		break;
	case MCA_PROP_SCENE:
		info->scene = value;
		break;
	case MCA_PROP_BOARD_TEMP:
		info->board_temp = value;
		break;
	case MCA_PROP_SMART_SIC_MODE:
		info->smart_sic_mode = value;
		break;
	default:
		mutex_unlock(&info->lock);
		return -EACCES;
	}
	mutex_unlock(&info->lock);

	if (apply_fv)
		mca_smartchg_apply_delta_fv(value);
	if (apply_ichg)
		mca_smartchg_apply_delta_ichg(value);
	return count;
}

static int smart_charge_probe(struct platform_device *pdev)
{
	struct smart_charge_info *info;
	u32 cell_type = 1;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	mutex_init(&info->lock);
	of_property_read_u32(pdev->dev.of_node, "cell_type", &cell_type);
	info->cell_type = cell_type ? cell_type : 1;
	info->extreme_cold_enabled =
		of_property_read_bool(pdev->dev.of_node, "support_extreme_cold");
	platform_set_drvdata(pdev, info);

	mca_sysfs_init_attrs(smartchg_sysfs_attrs, smartchg_sysfs_field_tbl,
			     SMARTCHG_SYSFS_ATTRS_SIZE);
	ret = mca_sysfs_create_link_group("charger", "smart_charge", &pdev->dev,
					  &smartchg_sysfs_attr_group);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to create smart_charge sysfs\n");
	dev_set_drvdata(&pdev->dev, info);
	global_smartchg_info = info;
	mca_log_info("smart-charge sysfs/callback core ready, cell_type=%d\n",
		     info->cell_type);
	return 0;
}

static int smart_charge_remove(struct platform_device *pdev)
{
	struct smart_charge_info *info = platform_get_drvdata(pdev);

	mca_sysfs_remove_link_group("charger", "smart_charge", &pdev->dev,
				    &smartchg_sysfs_attr_group);
	if (global_smartchg_info == info)
		global_smartchg_info = NULL;
	return 0;
}

static const struct of_device_id smart_charge_match_table[] = {
	{ .compatible = "xiaomi,smart_charge" },
	{},
};
MODULE_DEVICE_TABLE(of, smart_charge_match_table);

static struct platform_driver smart_charge_driver = {
	.driver = {
		.name = "smart_charge",
		.of_match_table = smart_charge_match_table,
	},
	.probe = smart_charge_probe,
	.remove = smart_charge_remove,
};
module_platform_driver(smart_charge_driver);

MODULE_DESCRIPTION("Xiaomi MCA smart-charge sysfs and callback core");
MODULE_LICENSE("GPL v2");
