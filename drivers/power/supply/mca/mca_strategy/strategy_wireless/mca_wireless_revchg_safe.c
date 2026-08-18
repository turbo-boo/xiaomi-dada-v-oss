// SPDX-License-Identifier: GPL-2.0
/* Safe reverse-wireless ABI baseline for Xiaomi Dada MCA. */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_wireless_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_wireless_revchg"
#endif

struct dada_rev_wireless {
	struct device *dev;
	struct mutex lock;
	bool wired_chg_ok;
	bool user_reverse_chg;
	bool reverse_chg_en;
	bool usb_plugin;
	bool fw_update;
	int reverse_chg_state;
	int firmware_state;
	int rev_boost_default;
};

static struct dada_rev_wireless *g_rev;

enum dada_rev_attr {
	REV_ATTR_WIRELESS_CHIP_FW = 0,
	REV_ATTR_WLS_FW_STATE,
	REV_ATTR_REVERSE_CHG_MODE,
	REV_ATTR_REVERSE_CHG_STATE,
	REV_ATTR_PEN_SOC,
	REV_ATTR_PEN_HALL3,
	REV_ATTR_PEN_HALL4,
	REV_ATTR_PEN_HALL3_S,
	REV_ATTR_PEN_HALL4_S,
	REV_ATTR_PEN_PPE_HALL_N,
	REV_ATTR_PEN_PPE_HALL_S,
	REV_ATTR_PEN_SS_VOLTAGE,
	REV_ATTR_TX_VOUT,
	REV_ATTR_TX_IOUT,
	REV_ATTR_TX_TDIE,
	REV_ATTR_PEN_PLACE_ERR,
};

int mca_wireless_rev_set_wired_chg_ok(bool ok)
{
	if (!g_rev)
		return -ENODEV;
	g_rev->wired_chg_ok = ok;
	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_set_wired_chg_ok);

int mca_wireless_rev_enable_reverse_charge(bool enable)
{
	int ret;

	if (!g_rev)
		return -ENODEV;

	/* The Dada external-boost path is not restored yet. Never source power
	 * merely by putting NU1652 into TX mode. Disable remains fully usable.
	 */
	if (enable)
		return -EOPNOTSUPP;

	ret = platform_class_wireless_enable_reverse_chg(WIRELESS_ROLE_MASTER,
							 false);
	if (!ret) {
		g_rev->reverse_chg_en = false;
		g_rev->reverse_chg_state = 0;
	}
	return ret;
}
EXPORT_SYMBOL(mca_wireless_rev_enable_reverse_charge);

int mca_wireless_rev_set_firmware_state(int state)
{
	if (!g_rev)
		return -ENODEV;
	g_rev->firmware_state = state;
	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_set_firmware_state);

int mca_wireless_rev_get_rev_boost_default(int *value)
{
	if (!g_rev || !value)
		return -EINVAL;
	*value = g_rev->rev_boost_default;
	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_get_rev_boost_default);

int mca_wireless_rev_get_reverse_chg(bool *enable)
{
	if (!g_rev || !enable)
		return -EINVAL;
	*enable = g_rev->reverse_chg_en;
	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_get_reverse_chg);

int mca_wireless_rev_get_reverse_chg_state(int *state)
{
	if (!g_rev || !state)
		return -EINVAL;
	*state = g_rev->reverse_chg_state;
	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_get_reverse_chg_state);

int mca_wireless_rev_get_user_reverse_chg(bool *enable)
{
	if (!g_rev || !enable)
		return -EINVAL;
	*enable = g_rev->user_reverse_chg;
	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_get_user_reverse_chg);

int mca_wireless_rev_set_user_reverse_chg(bool enable)
{
	int ret = 0;

	if (!g_rev)
		return -ENODEV;
	g_rev->user_reverse_chg = enable;
	if (!enable)
		ret = mca_wireless_rev_enable_reverse_charge(false);
	else
		ret = -EOPNOTSUPP;
	return ret;
}
EXPORT_SYMBOL(mca_wireless_rev_set_user_reverse_chg);

int mca_wireless_rev_update_fw_version(int cmd)
{
	/* Runtime NU1652 driver intentionally excludes MTP erase/programming. */
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(mca_wireless_rev_update_fw_version);

int mca_wireless_rev_set_usb_plugin(bool inserted)
{
	if (!g_rev)
		return -ENODEV;
	g_rev->usb_plugin = inserted;
	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_set_usb_plugin);

int mca_wireless_rev_get_fw_update(bool *fw_update)
{
	if (!g_rev || !fw_update)
		return -EINVAL;
	*fw_update = g_rev->fw_update;
	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_get_fw_update);

static int dada_rev_process(int event, int value, void *data)
{
	struct dada_rev_wireless *info = data;

	if (!info)
		return -EINVAL;
	/* Keep IRQ/event reception alive for later TX state-machine restoration. */
	if (event == MCA_EVENT_WIRELESS_INT_CHANGE)
		return 0;
	return -EOPNOTSUPP;
}

static int dada_rev_get_status(int status, void *value, void *data)
{
	struct dada_rev_wireless *info = data;

	if (!info || !value)
		return -EINVAL;
	if (status == STRATEGY_STATUS_TYPE_REV_TEST) {
		*(int *)value = 0;
		return 0;
	}
	return -EOPNOTSUPP;
}

static int dada_rev_read_int(int (*fn)(unsigned int, int *), int *value)
{
	if (!fn || !value)
		return -EINVAL;
	return fn(WIRELESS_ROLE_MASTER, value);
}

static ssize_t dada_rev_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct dada_rev_wireless *info = dev_get_drvdata(dev);
	int value = 0, ret;
	char fw[32] = { 0 };

	if (!info)
		return -ENODEV;

	if (!strcmp(attr->attr.name, "wireless_chip_fw")) {
		ret = platform_class_wireless_get_fw_version(WIRELESS_ROLE_MASTER, fw);
		return ret < 0 ? ret : sysfs_emit(buf, "%s\n", fw);
	}
	if (!strcmp(attr->attr.name, "wls_fw_state"))
		return sysfs_emit(buf, "%d\n", info->firmware_state);
	if (!strcmp(attr->attr.name, "reverse_chg_mode"))
		return sysfs_emit(buf, "%d\n", info->user_reverse_chg);
	if (!strcmp(attr->attr.name, "reverse_chg_state"))
		return sysfs_emit(buf, "%d\n", info->reverse_chg_state);
	if (!strcmp(attr->attr.name, "pen_soc"))
		ret = platform_class_wireless_get_pen_soc(WIRELESS_ROLE_MASTER, &value);
	else if (!strcmp(attr->attr.name, "pen_hall3"))
		ret = platform_class_wireless_get_pen_hall3(WIRELESS_ROLE_MASTER, &value);
	else if (!strcmp(attr->attr.name, "pen_hall4"))
		ret = platform_class_wireless_get_pen_hall4(WIRELESS_ROLE_MASTER, &value);
	else if (!strcmp(attr->attr.name, "pen_hall3_s"))
		ret = platform_class_wireless_get_pen_hall3_s(WIRELESS_ROLE_MASTER, &value);
	else if (!strcmp(attr->attr.name, "pen_hall4_s"))
		ret = platform_class_wireless_get_pen_hall4_s(WIRELESS_ROLE_MASTER, &value);
	else if (!strcmp(attr->attr.name, "pen_ppe_hall_n"))
		ret = platform_class_wireless_get_pen_hall_ppe_n(WIRELESS_ROLE_MASTER,
							       &value);
	else if (!strcmp(attr->attr.name, "pen_ppe_hall_s"))
		ret = platform_class_wireless_get_pen_hall_ppe_s(WIRELESS_ROLE_MASTER,
							       &value);
	else if (!strcmp(attr->attr.name, "pen_ss_voltage"))
		ret = platform_class_wireless_get_ss_voltage(WIRELESS_ROLE_MASTER,
							      &value);
	else if (!strcmp(attr->attr.name, "tx_vout"))
		ret = platform_class_wireless_get_tx_vout(WIRELESS_ROLE_MASTER, &value);
	else if (!strcmp(attr->attr.name, "tx_iout"))
		ret = platform_class_wireless_get_tx_iout(WIRELESS_ROLE_MASTER, &value);
	else if (!strcmp(attr->attr.name, "tx_tdie"))
		ret = platform_class_wireless_get_temp(WIRELESS_ROLE_MASTER, &value);
	else if (!strcmp(attr->attr.name, "pen_place_err"))
		ret = platform_class_wireless_get_pen_place_err(WIRELESS_ROLE_MASTER,
							      &value);
	else
		return -EINVAL;

	return ret ? ret : sysfs_emit(buf, "%d\n", value);
}

static ssize_t dada_rev_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct dada_rev_wireless *info = dev_get_drvdata(dev);
	int value, ret;

	if (!info)
		return -ENODEV;
	if (kstrtoint(buf, 0, &value))
		return -EINVAL;

	if (!strcmp(attr->attr.name, "reverse_chg_mode")) {
		ret = mca_wireless_rev_set_user_reverse_chg(!!value);
		return ret ? ret : count;
	}
	if (!strcmp(attr->attr.name, "wls_fw_state")) {
		info->firmware_state = value;
		return count;
	}
	if (!strcmp(attr->attr.name, "pen_place_err")) {
		ret = platform_class_wireless_set_pen_place_err(WIRELESS_ROLE_MASTER,
							      value);
		return ret ? ret : count;
	}
	if (!strcmp(attr->attr.name, "wireless_chip_fw"))
		return -EOPNOTSUPP;
	return -EACCES;
}

static struct mca_sysfs_attr_info dada_rev_attr_info[] = {
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_WIRELESS_CHIP_FW, wireless_chip_fw),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_WLS_FW_STATE, wls_fw_state),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_REVERSE_CHG_MODE, reverse_chg_mode),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_REVERSE_CHG_STATE, reverse_chg_state),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_PEN_SOC, pen_soc),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_PEN_HALL3, pen_hall3),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_PEN_HALL4, pen_hall4),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_PEN_HALL3_S, pen_hall3_s),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_PEN_HALL4_S, pen_hall4_s),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_PEN_PPE_HALL_N, pen_ppe_hall_n),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_PEN_PPE_HALL_S, pen_ppe_hall_s),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_PEN_SS_VOLTAGE, pen_ss_voltage),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_TX_VOUT, tx_vout),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_TX_IOUT, tx_iout),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_TX_TDIE, tx_tdie),
	mca_sysfs_attr_rw(dada_rev, 0664, REV_ATTR_PEN_PLACE_ERR, pen_place_err),
};

static struct attribute *dada_rev_attrs[ARRAY_SIZE(dada_rev_attr_info) + 1];
static const struct attribute_group dada_rev_group = { .attrs = dada_rev_attrs };

static int dada_rev_probe(struct platform_device *pdev)
{
	struct dada_rev_wireless *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	info->firmware_state = FIRMWARE_NO_UPDATE;
	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);
	dev_set_drvdata(&pdev->dev, info);
	g_rev = info;

	(void)of_property_read_u32(pdev->dev.of_node, "rev_boost_default",
				   &info->rev_boost_default);

	ret = mca_strategy_ops_register(STRATEGY_FUNC_TYPE_REV_WIRELESS,
					dada_rev_process, dada_rev_get_status,
					NULL, info);
	if (ret)
		goto err_global;

	mca_sysfs_init_attrs(dada_rev_attrs, dada_rev_attr_info,
			     ARRAY_SIZE(dada_rev_attr_info));
	ret = mca_sysfs_create_link_group("charger", "wls_rev_charge",
					  &pdev->dev, &dada_rev_group);
	if (ret)
		goto err_global;

	mca_log_info("safe reverse-wireless ABI registered\n");
	return 0;

err_global:
	g_rev = NULL;
	mutex_destroy(&info->lock);
	return ret;
}

static int dada_rev_remove(struct platform_device *pdev)
{
	struct dada_rev_wireless *info = platform_get_drvdata(pdev);

	if (!info)
		return 0;
	(void)mca_wireless_rev_enable_reverse_charge(false);
	mca_sysfs_remove_link_group("charger", "wls_rev_charge", &pdev->dev,
				    &dada_rev_group);
	if (g_rev == info)
		g_rev = NULL;
	mutex_destroy(&info->lock);
	return 0;
}

static const struct of_device_id dada_rev_match[] = {
	{ .compatible = "mca,wireless_revchg" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_rev_match);

static struct platform_driver dada_rev_driver = {
	.driver = {
		.name = "mca_wireless_revchg",
		.of_match_table = dada_rev_match,
	},
	.probe = dada_rev_probe,
	.remove = dada_rev_remove,
};
module_platform_driver(dada_rev_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA safe reverse wireless ABI baseline");
MODULE_LICENSE("GPL v2");
