// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi MCA basic wireless strategy for Dada bring-up.
 *
 * Keep the stock userspace/status ABI and event plumbing while leaving
 * high-power CP/FCC policy to the quick-wireless strategy. This layer tracks
 * attachment/authentication state, applies DT-provided FOD after a successful
 * RX exchange, and exposes the basic wireless controls.
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/smartchg/smart_chg_class.h>
#include <mca/strategy/strategy_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_basic_wireless"
#endif

struct dada_basic_wireless {
	struct device *dev;
	struct notifier_block connect_nb;
	struct mutex lock;
	bool initialized;
	bool online;
	bool fod_applied;
	bool quiet;
	bool super_charge;
	int soc_limit;
	int delta_fv;
	int audio_phone_sts;
	int set_rx_sleep;
	int low_inductance_offset;
	int car_adapter;
	int fc_flag;
	int adapter_type;
	int qc_type;
	int max_power;
	int auth_value;
	u8 power_mode;
	u8 uuid[4];
};

enum basic_wireless_attr {
	BASIC_WLS_DEBUG = 0,
	BASIC_WLS_CAR_ADAPTER,
	BASIC_WLS_FC_FLAG,
	BASIC_WLS_LOW_INDUCTANCE_OFFSET,
	BASIC_WLS_SET_RX_SLEEP,
	BASIC_WLS_AUDIO_PHONE_STS,
};

static int dada_basic_wireless_refresh(struct dada_basic_wireless *info,
					       bool apply_fod)
{
	u8 max_power = 0;
	u8 fc_flag = 0;
	bool car = false;
	int present = 0;
	int adapter = ADAPTER_NONE;
	int auth = 0;
	int ret;

	ret = platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &present);
	if (ret)
		return ret;
	info->online = !!present;
	if (!info->online) {
		info->adapter_type = ADAPTER_NONE;
		info->auth_value = 0;
		info->max_power = 0;
		info->qc_type = ADP_ICON_TYPE_NORMAL;
		info->power_mode = 0;
		info->fc_flag = 0;
		info->car_adapter = 0;
		info->fod_applied = false;
		memset(info->uuid, 0, sizeof(info->uuid));
		return 0;
	}

	ret = platform_class_wireless_get_tx_adapter_by_i2c(WIRELESS_ROLE_MASTER,
							    &adapter);
	if (!ret)
		info->adapter_type = adapter;

	ret = platform_class_wireless_get_auth_value(WIRELESS_ROLE_MASTER, &auth);
	if (!ret)
		info->auth_value = auth;

	ret = platform_class_wireless_get_rx_power_mode(WIRELESS_ROLE_MASTER,
							&info->power_mode);
	if (ret)
		info->power_mode = 0;

	ret = platform_class_wireless_get_tx_max_power(WIRELESS_ROLE_MASTER,
						       &max_power);
	if (!ret)
		info->max_power = max_power;

	ret = platform_class_wireless_get_rx_fastcharge_status(
		WIRELESS_ROLE_MASTER, &fc_flag);
	if (!ret)
		info->fc_flag = fc_flag;

	ret = platform_class_wireless_is_car_adapter(WIRELESS_ROLE_MASTER, &car);
	if (!ret)
		info->car_adapter = car;

	ret = platform_class_wireless_get_tx_uuid(WIRELESS_ROLE_MASTER,
						  info->uuid);
	if (ret)
		memset(info->uuid, 0, sizeof(info->uuid));

	/* Match the public MCA icon policy without changing charge current. */
	switch (info->adapter_type) {
	case ADAPTER_XIAOMI_QC3:
	case ADAPTER_XIAOMI_PD:
	case ADAPTER_ZIMI_CAR_POWER:
		info->qc_type = ADP_ICON_TYPE_FLASH;
		break;
	case ADAPTER_XIAOMI_PD_40W:
	case ADAPTER_VOICE_BOX:
	case ADAPTER_XIAOMI_PD_50W:
	case ADAPTER_XIAOMI_PD_60W:
	case ADAPTER_XIAOMI_PD_100W:
		info->qc_type = ADP_ICON_TYPE_SUPER;
		break;
	default:
		info->qc_type = ADP_ICON_TYPE_NORMAL;
		break;
	}

	/* FOD is applied only after the RX reports authentication data. */
	if (apply_fod && !info->fod_applied && info->auth_value > 0) {
		ret = platform_class_wireless_set_fod_params(WIRELESS_ROLE_MASTER, 0);
		if (!ret)
			info->fod_applied = true;
	}

	return 0;
}

static int dada_basic_wireless_process(int event, int value, void *data)
{
	struct dada_basic_wireless *info = data;
	int ret = 0;

	if (!info)
		return -ENODEV;

	mutex_lock(&info->lock);
	switch (event) {
	case MCA_EVENT_WIRELESS_CONNECT:
		info->online = true;
		info->fod_applied = false;
		ret = dada_basic_wireless_refresh(info, false);
		break;
	case MCA_EVENT_WIRELESS_DISCONNECT:
		info->online = false;
		info->fod_applied = false;
		ret = dada_basic_wireless_refresh(info, false);
		break;
	case MCA_EVENT_WIRELESS_INT_CHANGE:
		ret = dada_basic_wireless_refresh(info, true);
		break;
	case MCA_EVENT_WIRELESS_MAGNETIC_CASE_INT:
		info->fod_applied = false;
		break;
	default:
		break;
	}
	mutex_unlock(&info->lock);
	return ret;
}

static int dada_basic_wireless_get_status(int status, void *value, void *data)
{
	struct dada_basic_wireless *info = data;
	int *out = value;
	int present = 0;

	if (!info || !value)
		return -EINVAL;

	switch (status) {
	case STRATEGY_STATUS_TYPE_ONLINE:
		if (platform_class_wireless_is_present(WIRELESS_ROLE_MASTER,
							       &present))
			return -ENODATA;
		*out = present;
		break;
	case STRATEGY_STATUS_TYPE_CHARGING:
		*out = info->initialized;
		break;
	case STRATEGY_STATUS_TYPE_QC_TYPE:
		*out = info->qc_type;
		break;
	case STRATEGY_STATUS_TYPE_QC_ENABLE:
		*out = info->adapter_type >= ADAPTER_XIAOMI_QC3;
		break;
	case STRATEGY_STATUS_TYPE_POWER_MAX:
		*out = info->max_power;
		break;
	case STRATEGY_STATUS_TYPE_REV_TEST:
		*out = 0;
		break;
	case STRATEGY_STATUS_TYPE_WLS_MAGNET_LIMIT:
		*out = 0;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int dada_basic_wireless_connect_notifier(struct notifier_block *nb,
						 unsigned long event, void *data)
{
	struct dada_basic_wireless *info =
		container_of(nb, struct dada_basic_wireless, connect_nb);
	int online;

	if (event != MCA_EVENT_WIRELESS_CONNECT &&
	    event != MCA_EVENT_WIRELESS_DISCONNECT)
		return NOTIFY_DONE;
	online = event == MCA_EVENT_WIRELESS_CONNECT;
	(void)dada_basic_wireless_process(event, online, info);
	return NOTIFY_OK;
}

static int dada_basic_smart_delta_fv(void *data, int value)
{
	struct dada_basic_wireless *info = data;

	if (!info)
		return -EINVAL;
	info->delta_fv = value;
	return 0;
}

static int dada_basic_smart_soc_limit(void *data, int value)
{
	struct dada_basic_wireless *info = data;

	if (!info)
		return -EINVAL;
	info->soc_limit = value;
	return 0;
}

static int dada_basic_smart_quiet(void *data, int value)
{
	struct dada_basic_wireless *info = data;

	if (!info)
		return -EINVAL;
	info->quiet = !!value;
	return 0;
}

static int dada_basic_smart_super(void *data, int value)
{
	struct dada_basic_wireless *info = data;

	if (!info)
		return -EINVAL;
	info->super_charge = !!value;
	return 0;
}

static struct mca_smartchg_if_ops dada_basic_smart_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_WL_BUCK,
	.set_delta_fv = dada_basic_smart_delta_fv,
	.set_soc_limit_sts = dada_basic_smart_soc_limit,
	.set_wls_quiet_sts = dada_basic_smart_quiet,
	.set_wls_super_sts = dada_basic_smart_super,
};

static ssize_t dada_basic_wireless_sysfs_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct dada_basic_wireless *info = dev_get_drvdata(dev);
	int vout = 0, vrect = 0, iout = 0;

	if (!info)
		return -ENODEV;

	if (!strcmp(attr->attr.name, "wls_debug")) {
		(void)platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &vout);
		(void)platform_class_wireless_get_vrect(WIRELESS_ROLE_MASTER, &vrect);
		(void)platform_class_wireless_get_iout(WIRELESS_ROLE_MASTER, &iout);
		return sysfs_emit(buf, "vout=%d, vrect=%d, iout=%d\n",
				  vout, vrect, iout);
	}
	if (!strcmp(attr->attr.name, "wls_car_adapter"))
		return sysfs_emit(buf, "%d\n", info->car_adapter);
	if (!strcmp(attr->attr.name, "wls_fc_flag"))
		return sysfs_emit(buf, "%d\n", info->fc_flag);
	if (!strcmp(attr->attr.name, "low_inductance_offset"))
		return sysfs_emit(buf, "%d\n", info->low_inductance_offset);
	if (!strcmp(attr->attr.name, "set_rx_sleep"))
		return sysfs_emit(buf, "%d\n", info->set_rx_sleep);
	if (!strcmp(attr->attr.name, "audio_phone_sts"))
		return sysfs_emit(buf, "%d\n", info->audio_phone_sts);
	return -EINVAL;
}

static ssize_t dada_basic_wireless_sysfs_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct dada_basic_wireless *info = dev_get_drvdata(dev);
	int value, ret = 0;

	if (!info)
		return -ENODEV;
	if (kstrtoint(buf, 0, &value))
		return -EINVAL;

	if (!strcmp(attr->attr.name, "set_rx_sleep")) {
		info->set_rx_sleep = value;
		ret = platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER,
							      !value);
	} else if (!strcmp(attr->attr.name, "audio_phone_sts")) {
		info->audio_phone_sts = value;
		(void)mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
						MCA_EVENT_WIRELESS_AUDIO_PHONE_STS,
						value);
	} else if (!strcmp(attr->attr.name, "low_inductance_offset")) {
		info->low_inductance_offset = value;
		ret = platform_class_wireless_set_rx_offset(WIRELESS_ROLE_MASTER,
							     value);
	} else if (!strcmp(attr->attr.name, "wls_debug")) {
		return count;
	} else {
		return -EACCES;
	}
	return ret ? ret : count;
}

static struct mca_sysfs_attr_info dada_basic_wireless_attrs_info[] = {
	mca_sysfs_attr_rw(dada_basic_wireless_sysfs, 0664, BASIC_WLS_DEBUG,
			  wls_debug),
	mca_sysfs_attr_rw(dada_basic_wireless_sysfs, 0664,
			  BASIC_WLS_CAR_ADAPTER, wls_car_adapter),
	mca_sysfs_attr_rw(dada_basic_wireless_sysfs, 0664, BASIC_WLS_FC_FLAG,
			  wls_fc_flag),
	mca_sysfs_attr_rw(dada_basic_wireless_sysfs, 0664,
			  BASIC_WLS_LOW_INDUCTANCE_OFFSET, low_inductance_offset),
	mca_sysfs_attr_rw(dada_basic_wireless_sysfs, 0664,
			  BASIC_WLS_SET_RX_SLEEP, set_rx_sleep),
	mca_sysfs_attr_rw(dada_basic_wireless_sysfs, 0664,
			  BASIC_WLS_AUDIO_PHONE_STS, audio_phone_sts),
};

static struct attribute *dada_basic_wireless_attrs[
	ARRAY_SIZE(dada_basic_wireless_attrs_info) + 1];
static const struct attribute_group dada_basic_wireless_group = {
	.attrs = dada_basic_wireless_attrs,
};

static int dada_basic_wireless_probe(struct platform_device *pdev)
{
	struct dada_basic_wireless *info;
	int present = 0;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	info->qc_type = ADP_ICON_TYPE_NORMAL;
	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);
	dev_set_drvdata(&pdev->dev, info);

	ret = mca_strategy_ops_register(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
					dada_basic_wireless_process,
					dada_basic_wireless_get_status, NULL, info);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register basic wireless strategy\n");

	info->connect_nb.notifier_call = dada_basic_wireless_connect_notifier;
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
					      &info->connect_nb);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register wireless connect notifier\n");

	mca_sysfs_init_attrs(dada_basic_wireless_attrs,
			     dada_basic_wireless_attrs_info,
			     ARRAY_SIZE(dada_basic_wireless_attrs_info));
	ret = mca_sysfs_create_link_group("charger", "wls_basic_charge",
					  &pdev->dev,
					  &dada_basic_wireless_group);
	if (ret)
		goto err_notifier;

	dada_basic_smart_ops.data = info;
	(void)mca_smartchg_if_ops_register(&dada_basic_smart_ops);
	info->initialized = true;

	if (!platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &present) &&
	    present)
		(void)dada_basic_wireless_process(MCA_EVENT_WIRELESS_CONNECT, 1,
						 info);

	mca_log_info("basic wireless ABI ready online=%d\n", info->online);
	return 0;

err_notifier:
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
					  &info->connect_nb);
	return ret;
}

static int dada_basic_wireless_remove(struct platform_device *pdev)
{
	struct dada_basic_wireless *info = platform_get_drvdata(pdev);

	if (!info)
		return 0;
	mca_sysfs_remove_link_group("charger", "wls_basic_charge", &pdev->dev,
				    &dada_basic_wireless_group);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
					  &info->connect_nb);
	mutex_destroy(&info->lock);
	return 0;
}

static const struct of_device_id dada_basic_wireless_match[] = {
	{ .compatible = "mca,basic_wireless" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_basic_wireless_match);

static struct platform_driver dada_basic_wireless_driver = {
	.driver = {
		.name = "mca_basic_wireless",
		.of_match_table = dada_basic_wireless_match,
	},
	.probe = dada_basic_wireless_probe,
	.remove = dada_basic_wireless_remove,
};
module_platform_driver(dada_basic_wireless_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA basic wireless strategy");
MODULE_LICENSE("GPL v2");
