// SPDX-License-Identifier: GPL-2.0
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/protocol/protocol_pd_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "pd_auth"
#endif

#define PD_AUTH_MAX_VDM_WORDS 8
#define PD_AUTH_MAX_INPUT 256

enum pd_auth_attr_list {
	PD_AUTH_ATTR_NAME = 0,
	PD_AUTH_ATTR_REQUEST_VDM_CMD,
	PD_AUTH_ATTR_CURRENT_STATE,
	PD_AUTH_ATTR_ADAPTER_ID,
	PD_AUTH_ATTR_ADAPTER_SVID,
	PD_AUTH_ATTR_VERIFY_PROCESS,
	PD_AUTH_ATTR_VERIFIED,
	PD_AUTH_ATTR_CURRENT_PR,
	PD_AUTH_ATTR_IS_PD_ADAPTER,
	PD_AUTH_ATTR_DATA_ROLE,
};

struct pd_auth_strategy {
	struct device *dev;
	int port_num;
};

static ssize_t strategy_pd_auth_sysfs_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf);
static ssize_t strategy_pd_auth_sysfs_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count);

static struct mca_sysfs_attr_info strategy_pd_auth_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440,
			  PD_AUTH_ATTR_NAME, name),
	mca_sysfs_attr_rw(strategy_pd_auth_sysfs, 0664,
			  PD_AUTH_ATTR_REQUEST_VDM_CMD, request_vdm_cmd),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440,
			  PD_AUTH_ATTR_CURRENT_STATE, current_state),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440,
			  PD_AUTH_ATTR_ADAPTER_ID, adapter_id),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440,
			  PD_AUTH_ATTR_ADAPTER_SVID, adapter_svid),
	mca_sysfs_attr_rw(strategy_pd_auth_sysfs, 0664,
			  PD_AUTH_ATTR_VERIFY_PROCESS, verify_process),
	mca_sysfs_attr_rw(strategy_pd_auth_sysfs, 0664,
			  PD_AUTH_ATTR_VERIFIED, verified),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440,
			  PD_AUTH_ATTR_CURRENT_PR, current_pr),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440,
			  PD_AUTH_ATTR_IS_PD_ADAPTER, is_pd_adapter),
	mca_sysfs_attr_rw(strategy_pd_auth_sysfs, 0664,
			  PD_AUTH_ATTR_DATA_ROLE, data_role),
};

#define PD_AUTH_SYSFS_ATTRS_SIZE ARRAY_SIZE(strategy_pd_auth_sysfs_field_tbl)
static struct attribute *strategy_pd_auth_sysfs_attrs[PD_AUTH_SYSFS_ATTRS_SIZE + 1];
static const struct attribute_group strategy_pd_auth_sysfs_attr_group = {
	.attrs = strategy_pd_auth_sysfs_attrs,
};

static int pd_auth_parse_vdm_request(const char *input, enum uvdm_state *cmd,
				     unsigned int *words, unsigned int *nr_words)
{
	char *tmp, *cursor, *token;
	unsigned int n = 0;
	int value, ret = 0;

	if (!input || !cmd || !words || !nr_words)
		return -EINVAL;
	tmp = kstrdup(input, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	cursor = strim(tmp);
	token = strsep(&cursor, ", 	\n");
	while (token && !*token)
		token = strsep(&cursor, ", 	\n");
	if (!token || kstrtoint(token, 0, &value)) {
		ret = -EINVAL;
		goto out;
	}
	if (value < USBPD_UVDM_DISCONNECT || value > USBPD_UVDM_REQUEST_PD_DR) {
		ret = -ERANGE;
		goto out;
	}
	*cmd = value;

	while ((token = strsep(&cursor, ", 	\n")) != NULL) {
		unsigned int word;

		if (!*token)
			continue;
		if (n >= PD_AUTH_MAX_VDM_WORDS) {
			ret = -E2BIG;
			goto out;
		}
		if (kstrtouint(token, 0, &word)) {
			ret = -EINVAL;
			goto out;
		}
		words[n++] = word;
	}
	*nr_words = n;
out:
	kfree(tmp);
	return ret;
}

static ssize_t pd_auth_show_vdm(struct pd_auth_strategy *info, char *buf)
{
	struct usbpd_vdm_data vdm = { 0 };
	int cmd = USBPD_UVDM_DISCONNECT;
	ssize_t pos;
	int i, ret;

	ret = protocol_class_pd_get_vdm_cmd(info->port_num, &cmd, &vdm);
	if (ret)
		return ret;
	pos = sysfs_emit(buf, "%d", cmd);
	for (i = 0; i < ARRAY_SIZE(vdm.vdos) && pos < PAGE_SIZE - 12; i++)
		pos += sysfs_emit_at(buf, pos, ",%08x", vdm.vdos[i]);
	pos += sysfs_emit_at(buf, pos, "\n");
	return pos;
}

static bool pd_auth_has_pdo(struct pd_auth_strategy *info)
{
	struct pd_pdo pdos[PROTOCOL_PD_MAX_PDO_NUMS] = { 0 };
	int i;

	if (protocol_class_pd_get_pdos(info->port_num, pdos, ARRAY_SIZE(pdos)))
		return false;
	for (i = 0; i < ARRAY_SIZE(pdos); i++)
		if (pdos[i].max_volt > 0 && pdos[i].max_current > 0)
			return true;
	return false;
}

static ssize_t strategy_pd_auth_sysfs_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct pd_auth_strategy *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	unsigned int id = 0, svid = 0;
	unsigned char role = 0;
	char state[64] = { 0 };
	int value = 0, ret;

	if (!info)
		return -ENODEV;
	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  strategy_pd_auth_sysfs_field_tbl,
					  PD_AUTH_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case PD_AUTH_ATTR_NAME:
		return sysfs_emit(buf, "pd_auth\n");
	case PD_AUTH_ATTR_REQUEST_VDM_CMD:
		return pd_auth_show_vdm(info, buf);
	case PD_AUTH_ATTR_CURRENT_STATE:
		ret = protocol_class_pd_get_current_state(info->port_num,
							 state, sizeof(state));
		return ret ? ret : sysfs_emit(buf, "%s\n", state);
	case PD_AUTH_ATTR_ADAPTER_ID:
		ret = protocol_class_pd_get_adapter_id(info->port_num, &id);
		return ret ? ret : sysfs_emit(buf, "%04x\n", id);
	case PD_AUTH_ATTR_ADAPTER_SVID:
		ret = protocol_class_pd_get_adapter_svid(info->port_num, &svid);
		return ret ? ret : sysfs_emit(buf, "%04x\n", svid);
	case PD_AUTH_ATTR_VERIFY_PROCESS:
		ret = protocol_class_pd_get_verify_process(info->port_num, &value);
		return ret ? ret : sysfs_emit(buf, "%d\n", value);
	case PD_AUTH_ATTR_VERIFIED:
		ret = protocol_class_pd_get_pd_verifed(info->port_num, &value);
		return ret ? ret : sysfs_emit(buf, "%d\n", value);
	case PD_AUTH_ATTR_CURRENT_PR:
		ret = protocol_class_pd_get_power_role(info->port_num, &role);
		if (ret)
			return ret;
		if (role == 0)
			return sysfs_emit(buf, "sink\n");
		if (role == 1)
			return sysfs_emit(buf, "source\n");
		return sysfs_emit(buf, "none\n");
	case PD_AUTH_ATTR_IS_PD_ADAPTER:
		return sysfs_emit(buf, "%s\n", pd_auth_has_pdo(info) ? "true" : "false");
	case PD_AUTH_ATTR_DATA_ROLE:
		ret = protocol_class_pd_get_data_role(info->port_num, &role);
		if (ret)
			return ret;
		if (role == 1)
			return sysfs_emit(buf, "ufp\n");
		if (role == 2)
			return sysfs_emit(buf, "dfp\n");
		return sysfs_emit(buf, "unknown\n");
	default:
		return -EOPNOTSUPP;
	}
}

static int pd_auth_set_data_role(struct pd_auth_strategy *info, const char *buf)
{
	unsigned int word;

	if (sysfs_streq(buf, "ufp"))
		word = XM_REQUEST_PD_DR_UFP;
	else if (sysfs_streq(buf, "dfp"))
		word = XM_REQUEST_PD_DR_DFP;
	else
		return -EINVAL;
	return protocol_class_pd_request_vdm_cmd(info->port_num,
					 USBPD_UVDM_REQUEST_PD_DR,
					 &word, 1);
}

static ssize_t strategy_pd_auth_sysfs_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct pd_auth_strategy *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	unsigned int words[PD_AUTH_MAX_VDM_WORDS] = { 0 };
	unsigned int nr_words = 0;
	enum uvdm_state cmd;
	int value, ret;

	if (!info)
		return -ENODEV;
	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  strategy_pd_auth_sysfs_field_tbl,
					  PD_AUTH_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case PD_AUTH_ATTR_REQUEST_VDM_CMD:
		ret = pd_auth_parse_vdm_request(buf, &cmd, words, &nr_words);
		if (!ret)
			ret = protocol_class_pd_request_vdm_cmd(info->port_num, cmd,
							      words, nr_words);
		break;
	case PD_AUTH_ATTR_VERIFY_PROCESS:
		if (kstrtoint(buf, 0, &value) || value < 0 || value > 1)
			return -EINVAL;
		ret = protocol_class_pd_set_verify_process(info->port_num, value);
		break;
	case PD_AUTH_ATTR_VERIFIED:
		if (kstrtoint(buf, 0, &value) || value < 0 || value > 1)
			return -EINVAL;
		ret = protocol_class_pd_set_pd_verifed(info->port_num, value);
		if (!ret) {
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_TYPE,
					       MCA_EVENT_CHARGE_VERIFY_PROCESS_END,
					       &value);
			if (!value)
				mca_charge_mievent_report(CHARGE_DFX_PD_AUTH_FAILED,
							  NULL, 0);
		}
		break;
	case PD_AUTH_ATTR_DATA_ROLE:
		ret = pd_auth_set_data_role(info, buf);
		break;
	default:
		return -EACCES;
	}
	return ret ? ret : count;
}

static int strategy_pd_auth_probe(struct platform_device *pdev)
{
	struct pd_auth_strategy *info;
	int ports, ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	ports = protocol_class_pd_get_port_num();
	if (ports <= 0)
		return -EPROBE_DEFER;
	info->port_num = 0;
	platform_set_drvdata(pdev, info);

	mca_sysfs_init_attrs(strategy_pd_auth_sysfs_attrs,
			     strategy_pd_auth_sysfs_field_tbl,
			     PD_AUTH_SYSFS_ATTRS_SIZE);
	ret = mca_sysfs_create_link_group(SYSFS_DEV_3, "strategy_pd_auth",
					  &pdev->dev,
					  &strategy_pd_auth_sysfs_attr_group);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to create PD auth sysfs\n");
	dev_set_drvdata(&pdev->dev, info);
	mca_log_info("PD authentication ABI registered, ports=%d\n", ports);
	return 0;
}

static int strategy_pd_auth_remove(struct platform_device *pdev)
{
	mca_sysfs_remove_link_group(SYSFS_DEV_3, "strategy_pd_auth",
				    &pdev->dev,
				    &strategy_pd_auth_sysfs_attr_group);
	return 0;
}

static void strategy_pd_auth_shutdown(struct platform_device *pdev)
{
	struct pd_auth_strategy *info = platform_get_drvdata(pdev);

	if (info)
		protocol_class_pd_set_verify_process(info->port_num, 0);
}

static const struct of_device_id strategy_pd_auth_match[] = {
	{ .compatible = "mca,strategy_pd_auth" },
	{},
};
MODULE_DEVICE_TABLE(of, strategy_pd_auth_match);

static struct platform_driver strategy_pd_auth_driver = {
	.driver = {
		.name = "strategy_pd_auth",
		.of_match_table = strategy_pd_auth_match,
	},
	.probe = strategy_pd_auth_probe,
	.remove = strategy_pd_auth_remove,
	.shutdown = strategy_pd_auth_shutdown,
};
module_platform_driver(strategy_pd_auth_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA PD authentication strategy ABI");
MODULE_LICENSE("GPL v2");
