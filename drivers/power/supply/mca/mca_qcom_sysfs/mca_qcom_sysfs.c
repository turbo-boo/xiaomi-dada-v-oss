// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_bc12_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_qcom_sysfs"
#endif

struct mca_qcom_sysfs_dev {
	struct device *dev;
	struct device *typec_dev;
	struct class class;
	bool class_registered;
};

static const char * const usb_type_text[] = {
	[XM_CHARGER_TYPE_UNKNOW] = "Unknown",
	[XM_CHARGER_TYPE_SDP] = "SDP",
	[XM_CHARGER_TYPE_CDP] = "CDP",
	[XM_CHARGER_TYPE_DCP] = "DCP",
	[XM_CHARGER_TYPE_FLOAT] = "USB_FLOAT",
	[XM_CHARGER_TYPE_HVDCP2] = "HVDCP",
	[XM_CHARGER_TYPE_HVDCP3] = "HVDCP_3",
	[XM_CHARGER_TYPE_HVDCP3_B] = "HVDCP_3_B",
	[XM_CHARGER_TYPE_HVDCP3P5] = "HVDCP_3P5",
	[XM_CHARGER_TYPE_TYPEC] = "C",
	[XM_CHARGER_TYPE_PD] = "PD",
	[XM_CHARGER_TYPE_PD_VERIFY] = "PD_PPS",
	[XM_CHARGER_TYPE_PPS] = "PD_PPS",
	[XM_CHARGER_TYPE_RESERVED_13] = "Unknown",
	[XM_CHARGER_TYPE_ACA] = "ACA",
	[XM_CHARGER_TYPE_OCP] = "DCP",
};

static int mca_qcom_get_real_type(unsigned int *type)
{
	int bc12 = XM_CHARGER_TYPE_UNKNOW;
	unsigned int detected = XM_CHARGER_TYPE_UNKNOW;

	if (!type)
		return -EINVAL;

	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PPS, &detected) &&
	    detected != XM_CHARGER_TYPE_UNKNOW)
		goto out;
	detected = XM_CHARGER_TYPE_UNKNOW;
	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD, &detected) &&
	    detected != XM_CHARGER_TYPE_UNKNOW)
		goto out;
	detected = XM_CHARGER_TYPE_UNKNOW;
	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_QC, &detected) &&
	    detected != XM_CHARGER_TYPE_UNKNOW)
		goto out;
	if (platform_bc12_class_get_charge_type(BC12_MAIN_ROLE, &bc12))
		return -ENODATA;
	detected = bc12;
out:
	*type = detected;
	return 0;
}

static ssize_t real_type_show(const struct class *class,
			      const struct class_attribute *attr, char *buf)
{
	unsigned int type;
	int ret = mca_qcom_get_real_type(&type);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%s\n",
			  type < ARRAY_SIZE(usb_type_text) && usb_type_text[type] ?
			  usb_type_text[type] : "Unknown");
}
static CLASS_ATTR_RO(real_type);

static ssize_t usb_real_type_show(const struct class *class,
				  const struct class_attribute *attr, char *buf)
{
	return real_type_show(class, attr, buf);
}
static CLASS_ATTR_RO(usb_real_type);

static ssize_t authentic_show(const struct class *class,
			      const struct class_attribute *attr, char *buf)
{
	bool authentic = false;
	int ret = strategy_class_fg_get_authentic(&authentic);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", authentic);
}
static ssize_t authentic_store(const struct class *class,
			       const struct class_attribute *attr,
			       const char *buf, size_t count)
{
	return count;
}
static CLASS_ATTR_RW(authentic);

static ssize_t slave_authentic_show(const struct class *class,
				    const struct class_attribute *attr, char *buf)
{
	int ret = strategy_class_fg_dual_is_chip_ok(1);

	if (ret == -EOPNOTSUPP || ret == -ENODEV)
		return sysfs_emit(buf, "1\n");
	return sysfs_emit(buf, "%d\n", ret == 0);
}
static ssize_t slave_authentic_store(const struct class *class,
				     const struct class_attribute *attr,
				     const char *buf, size_t count)
{
	return count;
}
static CLASS_ATTR_RW(slave_authentic);

static ssize_t pd_verifed_show(const struct class *class,
			       const struct class_attribute *attr, char *buf)
{
	int verified = 0;
	int ret = protocol_class_get_adapter_verified(ADAPTER_PROTOCOL_PD,
						      &verified);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", verified);
}
static ssize_t pd_verifed_store(const struct class *class,
				const struct class_attribute *attr,
				const char *buf, size_t count)
{
	int verified;
	int ret;

	if (kstrtoint(buf, 10, &verified))
		return -EINVAL;
	ret = protocol_class_set_adapter_verified(ADAPTER_PROTOCOL_PD, verified);
	return ret ? ret : count;
}
static CLASS_ATTR_RW(pd_verifed);

static ssize_t quick_charge_type_show(const struct class *class,
				      const struct class_attribute *attr,
				      char *buf)
{
	int type = XM_CHARGER_TYPE_UNKNOW;

	if (mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					 STRATEGY_STATUS_TYPE_QC_TYPE, &type) ||
	    type == XM_CHARGER_TYPE_UNKNOW)
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					     STRATEGY_STATUS_TYPE_QC_TYPE, &type);
	return sysfs_emit(buf, "%d\n", type);
}
static CLASS_ATTR_RO(quick_charge_type);

static ssize_t power_max_show(const struct class *class,
			      const struct class_attribute *attr, char *buf)
{
	unsigned int power = 0;

	if (mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					 STRATEGY_STATUS_TYPE_QC_MAX_POWER,
					 &power) || !power)
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					     STRATEGY_STATUS_TYPE_POWER_MAX,
					     &power);
	return sysfs_emit(buf, "%u\n", power);
}
static CLASS_ATTR_RO(power_max);

static ssize_t soc_decimal_show(const struct class *class,
				const struct class_attribute *attr, char *buf)
{
	int decimal = 0, rate = 0;

	strategy_class_fg_ops_get_soc_decimal(&decimal, &rate);
	return sysfs_emit(buf, "%d\n", decimal);
}
static CLASS_ATTR_RO(soc_decimal);

static ssize_t soc_decimal_rate_show(const struct class *class,
				     const struct class_attribute *attr, char *buf)
{
	int decimal = 0, rate = 0;

	strategy_class_fg_ops_get_soc_decimal(&decimal, &rate);
	return sysfs_emit(buf, "%d\n", rate);
}
static CLASS_ATTR_RO(soc_decimal_rate);

static ssize_t otg_ui_support_show(const struct class *class,
				   const struct class_attribute *attr, char *buf)
{
	bool supported = false;
	int ret = platform_class_buckchg_ops_is_support_cid(MAIN_BUCK_CHARGER,
							    &supported);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", supported);
}
static CLASS_ATTR_RO(otg_ui_support);

static ssize_t cid_status_show(const struct class *class,
			       const struct class_attribute *attr, char *buf)
{
	bool status = false;
	int ret = protocol_class_pd_get_cid_status(TYPEC_PORT_0, &status);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", status);
}
static CLASS_ATTR_RO(cid_status);

static ssize_t cc_toggle_show(const struct class *class,
			      const struct class_attribute *attr, char *buf)
{
	bool enabled = false;
	int ret = protocol_class_pd_get_cc_toggle(TYPEC_PORT_0, &enabled);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", enabled);
}
static ssize_t cc_toggle_store(const struct class *class,
			       const struct class_attribute *attr,
			       const char *buf, size_t count)
{
	bool enabled;
	int ret;

	if (kstrtobool(buf, &enabled))
		return -EINVAL;
	ret = protocol_class_pd_set_cc_toggle(TYPEC_PORT_0, enabled);
	return ret ? ret : count;
}
static CLASS_ATTR_RW(cc_toggle);

static ssize_t has_dp_show(const struct class *class,
			   const struct class_attribute *attr, char *buf)
{
	bool has_dp = false;
	int ret = protocol_class_pd_get_has_dp(TYPEC_PORT_0, &has_dp);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", has_dp);
}
static CLASS_ATTR_RO(has_dp);

static ssize_t dam_ovpgate_show(const struct class *class,
				const struct class_attribute *attr, char *buf)
{
	bool enabled = false;
	int ret = platform_class_cp_get_ovpgate_status(CP_ROLE_MASTER, &enabled);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", enabled);
}
static ssize_t dam_ovpgate_store(const struct class *class,
				 const struct class_attribute *attr,
				 const char *buf, size_t count)
{
	bool enabled;
	int ret;

	if (kstrtobool(buf, &enabled))
		return -EINVAL;
	ret = platform_class_cp_enable_ovpgate(CP_ROLE_MASTER, enabled);
	return ret ? ret : count;
}
static CLASS_ATTR_RW(dam_ovpgate);

static ssize_t pmic_ibat_show(const struct class *class,
			      const struct class_attribute *attr, char *buf)
{
	int ibat = 0;
	int ret = platform_class_buckchg_ops_get_pack_ibat(MAIN_BUCK_CHARGER,
							   &ibat);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", ibat);
}
static CLASS_ATTR_RO(pmic_ibat);

static ssize_t soh_show(const struct class *class,
			const struct class_attribute *attr, char *buf)
{
	int soh = 0;
	int ret = strategy_class_fg_get_soh(&soh);

	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", soh);
}
static CLASS_ATTR_RO(soh);

static struct attribute *mca_qcom_sysfs_attrs[] = {
	&class_attr_real_type.attr,
	&class_attr_usb_real_type.attr,
	&class_attr_authentic.attr,
	&class_attr_slave_authentic.attr,
	&class_attr_pd_verifed.attr,
	&class_attr_quick_charge_type.attr,
	&class_attr_power_max.attr,
	&class_attr_soc_decimal.attr,
	&class_attr_soc_decimal_rate.attr,
	&class_attr_otg_ui_support.attr,
	&class_attr_cid_status.attr,
	&class_attr_cc_toggle.attr,
	&class_attr_has_dp.attr,
	&class_attr_dam_ovpgate.attr,
	&class_attr_pmic_ibat.attr,
	&class_attr_soh.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mca_qcom_sysfs);

enum dada_typec_attr {
	DADA_TYPEC_APDO_MAX = 0,
	DADA_TYPEC_HAS_DP,
	DADA_TYPEC_CID_STATUS,
	DADA_TYPEC_OTG_UI_SUPPORT,
	DADA_TYPEC_CC_TOGGLE,
};

static ssize_t dada_typec_show(struct device *dev,
			       struct device_attribute *attr, char *buf);
static ssize_t dada_typec_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);

static struct mca_sysfs_attr_info dada_typec_fields[] = {
	mca_sysfs_attr_ro(dada_typec, 0440, DADA_TYPEC_APDO_MAX, apdo_max),
	mca_sysfs_attr_ro(dada_typec, 0440, DADA_TYPEC_HAS_DP, has_dp),
	mca_sysfs_attr_ro(dada_typec, 0440, DADA_TYPEC_CID_STATUS, cid_status),
	mca_sysfs_attr_ro(dada_typec, 0440, DADA_TYPEC_OTG_UI_SUPPORT, otg_ui_support),
	mca_sysfs_attr_rw(dada_typec, 0640, DADA_TYPEC_CC_TOGGLE, cc_toggle),
};
#define DADA_TYPEC_ATTR_COUNT ARRAY_SIZE(dada_typec_fields)
static struct attribute *dada_typec_attrs[DADA_TYPEC_ATTR_COUNT + 1];
static const struct attribute_group dada_typec_group = { .attrs = dada_typec_attrs };

static ssize_t dada_typec_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *field;
	unsigned int apdo = 0;
	bool value = false;
	int ret = 0;

	field = mca_sysfs_lookup_attr(attr->attr.name, dada_typec_fields,
				      DADA_TYPEC_ATTR_COUNT);
	if (!field)
		return -EINVAL;

	switch (field->sysfs_attr_name) {
	case DADA_TYPEC_APDO_MAX:
		ret = protocol_class_pd_get_pps_apdo_max(TYPEC_PORT_0, &apdo);
		if (ret || !apdo) {
			apdo = 0;
			(void)protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PPS,
							     &apdo);
		}
		return sysfs_emit(buf, "%u\n", apdo);
	case DADA_TYPEC_HAS_DP:
		ret = protocol_class_pd_get_has_dp(TYPEC_PORT_0, &value);
		break;
	case DADA_TYPEC_CID_STATUS:
		ret = protocol_class_pd_get_cid_status(TYPEC_PORT_0, &value);
		break;
	case DADA_TYPEC_OTG_UI_SUPPORT:
		/* Stock Dada protocol_pd_class exposes this capability as 1. */
		value = true;
		break;
	case DADA_TYPEC_CC_TOGGLE:
		ret = protocol_class_pd_get_cc_toggle(TYPEC_PORT_0, &value);
		break;
	default:
		return -EINVAL;
	}
	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t dada_typec_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *field;
	bool value;
	int ret;

	field = mca_sysfs_lookup_attr(attr->attr.name, dada_typec_fields,
				      DADA_TYPEC_ATTR_COUNT);
	if (!field || field->sysfs_attr_name != DADA_TYPEC_CC_TOGGLE)
		return -EACCES;
	if (kstrtobool(buf, &value))
		return -EINVAL;
	ret = protocol_class_pd_set_cc_toggle(TYPEC_PORT_0, value);
	return ret ? ret : count;
}

static int mca_qcom_sysfs_probe(struct platform_device *pdev)
{
	struct mca_qcom_sysfs_dev *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	info->class.name = "qcom-battery";
	info->class.class_groups = mca_qcom_sysfs_groups;
	platform_set_drvdata(pdev, info);

	ret = class_register(&info->class);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register qcom-battery class\n");
	info->class_registered = true;

	mca_sysfs_init_attrs(dada_typec_attrs, dada_typec_fields,
			     DADA_TYPEC_ATTR_COUNT);
	info->typec_dev = mca_sysfs_create_group("xm_power", SYSFS_DEV_3,
						 &dada_typec_group);
	if (!info->typec_dev) {
		class_unregister(&info->class);
		info->class_registered = false;
		return -ENODEV;
	}

	mca_log_info("qcom-battery and stock Type-C sysfs registered\n");
	return 0;
}

static int mca_qcom_sysfs_remove(struct platform_device *pdev)
{
	struct mca_qcom_sysfs_dev *info = platform_get_drvdata(pdev);

	if (info && info->typec_dev) {
		mca_sysfs_remove_group("xm_power", info->typec_dev, &dada_typec_group);
		info->typec_dev = NULL;
	}
	if (info && info->class_registered) {
		class_unregister(&info->class);
		info->class_registered = false;
	}
	return 0;
}

static const struct of_device_id mca_qcom_sysfs_match[] = {
	{ .compatible = "mca,qcom_sysfs" },
	{},
};
MODULE_DEVICE_TABLE(of, mca_qcom_sysfs_match);

static struct platform_driver mca_qcom_sysfs_driver = {
	.driver = {
		.name = "mca_qcom_sysfs",
		.of_match_table = mca_qcom_sysfs_match,
	},
	.probe = mca_qcom_sysfs_probe,
	.remove = mca_qcom_sysfs_remove,
};
module_platform_driver(mca_qcom_sysfs_driver);

MODULE_DESCRIPTION("Xiaomi Dada qcom-battery and Type-C compatibility sysfs");
MODULE_LICENSE("GPL v2");
