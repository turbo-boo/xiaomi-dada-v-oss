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

#define PD_ROLE_SINK_FOR_ADAPTER 0
#define PD_ROLE_SOURCE_FOR_ADAPTER 1
#define PD_AUTH_UVDM_AUTH_DATA_LEN 16
#define PD_AUTH_UVDM_AUTH_STR_LEN 128
#define PD_AUTH_VDM_CMD_HEX_DATA_LEN 40
#define XM_ADAPTER_SVID 0x2717

enum pd_auth_attr_list {
	PD_AUTH_NAME = 0,
	PD_AUTH_REQUEST_VDM_CMD,
	PD_AUTH_CURRENT_STATE,
	PD_AUTH_ADAPTER_ID,
	PD_AUTH_ADAPTER_SVID,
	PD_AUTH_VERIFY_PROCESS,
	PD_AUTH_USBPD_VERIFIED,
	PD_AUTH_CURRENT_PR,
	PD_AUTH_IS_PD_ADAPTER,
	PD_AUTH_USBPD_DATA_ROLE,
};

struct pd_auth_strategy {
	struct device *dev;
	int verify_porcess_end;
	int pd_verified_type;
};

static ssize_t strategy_pd_auth_sysfs_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf);
static ssize_t strategy_pd_auth_sysfs_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count);

static struct mca_sysfs_attr_info strategy_pd_auth_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440, PD_AUTH_NAME, name),
	mca_sysfs_attr_rw(strategy_pd_auth_sysfs, 0664, PD_AUTH_REQUEST_VDM_CMD,
			  request_vdm_cmd),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440, PD_AUTH_CURRENT_STATE,
			  current_state),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440, PD_AUTH_ADAPTER_ID,
			  adapter_id),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440, PD_AUTH_ADAPTER_SVID,
			  adapter_svid),
	mca_sysfs_attr_rw(strategy_pd_auth_sysfs, 0664, PD_AUTH_VERIFY_PROCESS,
			  verify_process),
	mca_sysfs_attr_rw(strategy_pd_auth_sysfs, 0664, PD_AUTH_USBPD_VERIFIED,
			  verified),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440, PD_AUTH_CURRENT_PR,
			  current_pr),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440, PD_AUTH_IS_PD_ADAPTER,
			  is_pd_adapter),
	mca_sysfs_attr_rw(strategy_pd_auth_sysfs, 0664, PD_AUTH_USBPD_DATA_ROLE,
			  data_role),
};

#define PD_AUTH_ATTRS_SIZE ARRAY_SIZE(strategy_pd_auth_sysfs_field_tbl)
static struct attribute *strategy_pd_auth_sysfs_attrs[PD_AUTH_ATTRS_SIZE + 1];
static const struct attribute_group strategy_pd_auth_sysfs_attr_group = {
	.attrs = strategy_pd_auth_sysfs_attrs,
};

static int strategy_pd_auth_get_vdm_cmd(char *buf, int active_port)
{
	struct usbpd_vdm_data vdm_data = { 0 };
	char data[PD_AUTH_UVDM_AUTH_DATA_LEN] = { 0 };
	char str_buf[PD_AUTH_UVDM_AUTH_STR_LEN] = { 0 };
	int cmd = USBPD_UVDM_DISCONNECT;
	int i, ret;

	ret = protocol_class_pd_get_vdm_cmd(active_port, &cmd, &vdm_data);
	if (ret)
		return ret;

	switch (cmd) {
	case USBPD_UVDM_CHARGER_VERSION:
		return sysfs_emit(buf, "%d,%x\n", cmd, vdm_data.ta_version);
	case USBPD_UVDM_CHARGER_TEMP:
		return sysfs_emit(buf, "%d,%d\n", cmd, vdm_data.ta_temp);
	case USBPD_UVDM_CHARGER_VOLTAGE:
		return sysfs_emit(buf, "%d,%d\n", cmd, vdm_data.ta_voltage);
	case USBPD_UVDM_SESSION_SEED:
	case USBPD_UVDM_CONNECT:
	case USBPD_UVDM_DISCONNECT:
	case USBPD_UVDM_VERIFIED:
	case USBPD_UVDM_REMOVE_COMPENSATION:
	case USBPD_UVDM_NAN_ACK:
		return sysfs_emit(buf, "%d,Null\n", cmd);
	case USBPD_UVDM_REVERSE_AUTHEN:
		return sysfs_emit(buf, "%d,%d\n", cmd, vdm_data.reauth);
	case USBPD_UVDM_AUTHENTICATION:
		for (i = 0; i < USBPD_UVDM_AUTH_WORDS; i++) {
			scnprintf(data, sizeof(data), "%08lx",
				  vdm_data.s_secert[USBPD_UVDM_AUTH_FIRST + i]);
			strlcat(str_buf, data, sizeof(str_buf));
		}
		return sysfs_emit(buf, "%d,%s\n", cmd, str_buf);
	default:
		if ((cmd >= USBPD_UVDM_CMD_INIT &&
		     cmd <= USBPD_UVDM_CMD_INIT + USBPD_UVDM_CONNECT) ||
		    (cmd >= USBPD_UVDM_CMD_NAK &&
		     cmd <= USBPD_UVDM_CMD_NAK + USBPD_UVDM_CONNECT))
			return sysfs_emit(buf, "%d,Null\n", cmd);
		mca_log_err("feedback cmd:%d is not supported\n", cmd);
		return sysfs_emit(buf, "%d,\n", cmd);
	}
}

static int strategy_pd_auth_is_pd_adapter(char *buf, int active_port)
{
	struct pd_pdo received_pdos[PROTOCOL_PD_MAX_PDO_NUMS] = { 0 };

	if (protocol_class_pd_get_pdos(active_port, received_pdos,
				       PROTOCOL_PD_MAX_PDO_NUMS))
		return sysfs_emit(buf, "false\n");
	return sysfs_emit(buf, "%s\n",
			  received_pdos[1].max_volt ? "true" : "false");
}

static ssize_t strategy_pd_auth_sysfs_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct pd_auth_strategy *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	unsigned int adapter_id = 0, adapter_svid = 0;
	unsigned char role = 0;
	char current_state[PROTOCOL_PD_MAX_STRING_LEN] = { 0 };
	int active_port = protocol_class_pd_get_port_num();
	int verify_process = 0, verified = 0;

	if (!info)
		return -ENODEV;
	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  strategy_pd_auth_sysfs_field_tbl,
					  PD_AUTH_ATTRS_SIZE);
	if (!attr_info)
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case PD_AUTH_NAME:
		return 0;
	case PD_AUTH_REQUEST_VDM_CMD:
		return strategy_pd_auth_get_vdm_cmd(buf, active_port);
	case PD_AUTH_CURRENT_STATE:
		(void)protocol_class_pd_get_current_state(active_port, current_state,
						     sizeof(current_state));
		return sysfs_emit(buf, "%s\n", current_state);
	case PD_AUTH_ADAPTER_ID:
		(void)protocol_class_pd_get_adapter_id(active_port, &adapter_id);
		return sysfs_emit(buf, "%08x\n", adapter_id);
	case PD_AUTH_ADAPTER_SVID:
		(void)protocol_class_pd_get_adapter_svid(active_port, &adapter_svid);
		return sysfs_emit(buf, "%04x\n", adapter_svid);
	case PD_AUTH_VERIFY_PROCESS:
		(void)protocol_class_pd_get_verify_process(active_port, &verify_process);
		return sysfs_emit(buf, "%d\n", verify_process);
	case PD_AUTH_USBPD_VERIFIED:
		(void)protocol_class_pd_get_pd_verifed(active_port, &verified);
		return sysfs_emit(buf, "%d\n", verified);
	case PD_AUTH_CURRENT_PR:
		if (protocol_class_pd_get_power_role(active_port, &role))
			return sysfs_emit(buf, "none\n");
		if (role == PD_ROLE_SINK_FOR_ADAPTER)
			return sysfs_emit(buf, "sink\n");
		if (role == PD_ROLE_SOURCE_FOR_ADAPTER)
			return sysfs_emit(buf, "source\n");
		return sysfs_emit(buf, "none\n");
	case PD_AUTH_IS_PD_ADAPTER:
		return strategy_pd_auth_is_pd_adapter(buf, active_port);
	case PD_AUTH_USBPD_DATA_ROLE:
		if (protocol_class_pd_get_data_role(active_port, &role))
			return sysfs_emit(buf, "unknown\n");
		if (role == XM_REQUEST_PD_DR_UFP)
			return sysfs_emit(buf, "ufp\n");
		if (role == XM_REQUEST_PD_DR_DFP)
			return sysfs_emit(buf, "dfp\n");
		return sysfs_emit(buf, "unknown\n");
	default:
		return 0;
	}
}

static int pd_auth_hex_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -EINVAL;
}

static int strategy_pd_auth_string2hex(const char *str, unsigned char *out,
				       size_t out_size, unsigned int *outlen)
{
	size_t len, i = 0, n = 0;

	if (!str || !out || !outlen)
		return -EINVAL;
	len = strcspn(str, "\r\n");
	if (!len || len > out_size * 2)
		return -EINVAL;

	if (len & 1) {
		int low = pd_auth_hex_nibble(str[0]);
		if (low < 0)
			return low;
		out[n++] = low;
		i = 1;
	}
	for (; i < len; i += 2) {
		int high = pd_auth_hex_nibble(str[i]);
		int low = pd_auth_hex_nibble(str[i + 1]);
		if (high < 0 || low < 0)
			return -EINVAL;
		out[n++] = (high << 4) | low;
	}
	*outlen = n;
	return 0;
}

static int strategy_pd_auth_set_vdm_cmd(const char *buf, int active_port)
{
	unsigned char data[PD_AUTH_VDM_CMD_HEX_DATA_LEN] = { 0 };
	char *tmp, *payload;
	unsigned int byte_count = 0;
	int cmd, ret;

	tmp = kstrdup(buf, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	payload = strchr(tmp, ',');
	if (!payload) {
		ret = -EINVAL;
		goto out;
	}
	*payload++ = '\0';
	if (kstrtoint(strim(tmp), 10, &cmd)) {
		ret = -EINVAL;
		goto out;
	}
	payload = strim(payload);
	if (sysfs_streq(payload, "Null")) {
		byte_count = 0;
	} else {
		ret = strategy_pd_auth_string2hex(payload, data, sizeof(data),
						  &byte_count);
		if (ret)
			goto out;
	}

	/* The stock transport length is a byte count, not a u32-word count. */
	ret = protocol_class_pd_request_vdm_cmd(active_port, cmd,
						(unsigned int *)data, byte_count);
out:
	kfree(tmp);
	return ret;
}

static void strategy_pd_auth_fail_report_dfx(void)
{
	int active_port = protocol_class_pd_get_port_num();
	unsigned int adapter_id = 0, adapter_svid = 0;
	int online = 0;

	(void)platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &online);
	if (!online)
		return;
	(void)protocol_class_pd_get_adapter_svid(active_port, &adapter_svid);
	if (adapter_svid != XM_ADAPTER_SVID)
		return;
	(void)protocol_class_pd_get_adapter_id(active_port, &adapter_id);
	mca_charge_mievent_report(CHARGE_DFX_PD_AUTH_FAILED, &adapter_id, 1);
}

static ssize_t strategy_pd_auth_sysfs_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct pd_auth_strategy *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	int active_port = protocol_class_pd_get_port_num();
	int value = 0, ret = 0;

	if (!info)
		return -ENODEV;
	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  strategy_pd_auth_sysfs_field_tbl,
					  PD_AUTH_ATTRS_SIZE);
	if (!attr_info)
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case PD_AUTH_REQUEST_VDM_CMD:
		ret = strategy_pd_auth_set_vdm_cmd(buf, active_port);
		break;
	case PD_AUTH_VERIFY_PROCESS:
		if (kstrtoint(buf, 10, &value))
			return -EINVAL;
		ret = protocol_class_pd_set_verify_process(active_port, value);
		if (!ret) {
			info->verify_porcess_end = value;
			if (!value)
				mca_event_block_notify(
					MCA_EVENT_TYPE_CHARGE_TYPE,
					MCA_EVENT_CHARGE_VERIFY_PROCESS_END,
					&info->verify_porcess_end);
		}
		break;
	case PD_AUTH_USBPD_VERIFIED:
		if (kstrtoint(buf, 10, &value))
			return -EINVAL;
		ret = protocol_class_pd_set_pd_verifed(active_port, value);
		if (!ret && value) {
			info->pd_verified_type = XM_CHARGER_TYPE_PD_VERIFY;
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_TYPE,
					       MCA_EVENT_CHARGE_TYPE_CHANGE,
					       &info->pd_verified_type);
		} else if (!ret) {
			strategy_pd_auth_fail_report_dfx();
		}
		break;
	case PD_AUTH_USBPD_DATA_ROLE:
		if (strncmp(buf, "ufp", 3) == 0)
			value = XM_REQUEST_PD_DR_UFP;
		else if (strncmp(buf, "dfp", 3) == 0)
			value = XM_REQUEST_PD_DR_DFP;
		else
			return -EINVAL;
		ret = protocol_class_pd_request_vdm_cmd(TYPEC_PORT_0,
							USBPD_UVDM_REQUEST_PD_DR,
							&value, sizeof(value));
		break;
	default:
		return -EACCES;
	}
	return ret ? ret : count;
}

static int strategy_pd_auth_probe(struct platform_device *pdev)
{
	struct pd_auth_strategy *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	info->verify_porcess_end = 1;
	info->pd_verified_type = 0;
	platform_set_drvdata(pdev, info);

	mca_sysfs_init_attrs(strategy_pd_auth_sysfs_attrs,
			     strategy_pd_auth_sysfs_field_tbl,
			     PD_AUTH_ATTRS_SIZE);
	ret = mca_sysfs_create_link_group(SYSFS_DEV_3, "strategy_pd_auth",
					  &pdev->dev,
					  &strategy_pd_auth_sysfs_attr_group);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to create PD auth sysfs\n");
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

MODULE_DESCRIPTION("Xiaomi Dada MCA PD authentication strategy");
MODULE_LICENSE("GPL v2");
