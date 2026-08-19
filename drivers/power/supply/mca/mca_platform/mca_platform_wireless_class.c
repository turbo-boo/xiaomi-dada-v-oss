// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_wireless_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "wireless_class"
#endif

#define MCA_WIRELESS_MAX_NUM 2
#define WLS_OP(role, member) ({ \
	struct platform_wireless_class_ops_data *__d = wls_get(role); \
	(__d && __d->ops && __d->ops->member) ? __d : NULL; \
})

struct platform_wireless_class_ops_data {
	struct platform_class_wireless_ops *ops;
	void *data;
};

struct platform_wireless_dev {
	struct device *dev;
	int wireless_num;
	const char *wireless_dir_list[MCA_WIRELESS_MAX_NUM];
	struct device *sysfs_dev[MCA_WIRELESS_MAX_NUM];
	int wireless_dev_index[MCA_WIRELESS_MAX_NUM];
};

static struct platform_wireless_class_ops_data wls_data[WIRELESS_ROLE_MAX];

static struct platform_wireless_class_ops_data *wls_get(unsigned int role)
{
	if (role >= WIRELESS_ROLE_MAX || !wls_data[role].ops)
		return NULL;
	return &wls_data[role];
}

int platform_class_wireless_register_ops(unsigned int role, void *data,
					 struct platform_class_wireless_ops *ops)
{
	if (role >= WIRELESS_ROLE_MAX || !data || !ops)
		return -EINVAL;
	wls_data[role].data = data;
	wls_data[role].ops = ops;
	return 0;
}
EXPORT_SYMBOL(platform_class_wireless_register_ops);

#define WLS_WRAP_SET_BOOL(fn, member) \
int fn(unsigned int role, bool value) { \
	struct platform_wireless_class_ops_data *d = WLS_OP(role, member); \
	return d ? d->ops->member(value, d->data) : -EOPNOTSUPP; \
} EXPORT_SYMBOL(fn)

#define WLS_WRAP_SET_INT(fn, member) \
int fn(unsigned int role, int value) { \
	struct platform_wireless_class_ops_data *d = WLS_OP(role, member); \
	return d ? d->ops->member(value, d->data) : -EOPNOTSUPP; \
} EXPORT_SYMBOL(fn)

#define WLS_WRAP_SET_U8(fn, member) \
int fn(unsigned int role, u8 value) { \
	struct platform_wireless_class_ops_data *d = WLS_OP(role, member); \
	return d ? d->ops->member(value, d->data) : -EOPNOTSUPP; \
} EXPORT_SYMBOL(fn)

#define WLS_WRAP_GET_INT(fn, member) \
int fn(unsigned int role, int *value) { \
	struct platform_wireless_class_ops_data *d = WLS_OP(role, member); \
	return d ? d->ops->member(value, d->data) : -EOPNOTSUPP; \
} EXPORT_SYMBOL(fn)

#define WLS_WRAP_GET_BOOL(fn, member) \
int fn(unsigned int role, bool *value) { \
	struct platform_wireless_class_ops_data *d = WLS_OP(role, member); \
	return d ? d->ops->member(value, d->data) : -EOPNOTSUPP; \
} EXPORT_SYMBOL(fn)

#define WLS_WRAP_GET_U8(fn, member) \
int fn(unsigned int role, u8 *value) { \
	struct platform_wireless_class_ops_data *d = WLS_OP(role, member); \
	return d ? d->ops->member(value, d->data) : -EOPNOTSUPP; \
} EXPORT_SYMBOL(fn)

#define WLS_WRAP_NOARG(fn, member) \
int fn(unsigned int role) { \
	struct platform_wireless_class_ops_data *d = WLS_OP(role, member); \
	return d ? d->ops->member(d->data) : -EOPNOTSUPP; \
} EXPORT_SYMBOL(fn)

WLS_WRAP_SET_BOOL(platform_class_wireless_enable_reverse_chg, wls_enable_reverse_chg);
WLS_WRAP_GET_INT(platform_class_wireless_is_present, wls_is_present);
WLS_WRAP_SET_INT(platform_class_wireless_set_vout, wls_set_vout);
WLS_WRAP_GET_INT(platform_class_wireless_get_vout, wls_get_vout);
WLS_WRAP_GET_INT(platform_class_wireless_get_iout, wls_get_iout);
WLS_WRAP_GET_INT(platform_class_wireless_get_vrect, wls_get_vrect);
WLS_WRAP_GET_INT(platform_class_wireless_get_tx_adapter, wls_get_tx_adapter);
WLS_WRAP_GET_INT(platform_class_wireless_get_tx_adapter_by_i2c, wls_get_tx_adapter_by_i2c);
WLS_WRAP_GET_INT(platform_class_wireless_get_rsv_eppmode_fail, wls_get_rsv_eppmode_fail);
WLS_WRAP_GET_INT(platform_class_wireless_get_temp, wls_get_temp);
WLS_WRAP_SET_BOOL(platform_class_wireless_set_enable_mode, wls_set_enable_mode);
WLS_WRAP_GET_BOOL(platform_class_wireless_is_car_adapter, wls_is_car_adapter);
WLS_WRAP_GET_INT(platform_class_wireless_get_rx_rtx_mode, wls_get_rx_rtx_mode);
WLS_WRAP_SET_INT(platform_class_wireless_set_input_current_limit, wls_set_input_current_limit);
WLS_WRAP_GET_INT(platform_class_wireless_get_rx_int_flag, wls_get_rx_int_flag);
WLS_WRAP_GET_U8(platform_class_wireless_get_rx_power_mode, wls_get_rx_power_mode);
WLS_WRAP_GET_U8(platform_class_wireless_get_tx_max_power, wls_get_tx_max_power);
WLS_WRAP_GET_INT(platform_class_wireless_get_auth_value, wls_get_auth_value);
WLS_WRAP_SET_INT(platform_class_wireless_set_adapter_voltage, wls_set_adapter_voltage);
WLS_WRAP_SET_INT(platform_class_wireless_set_fod_params, wls_set_fod_params);
WLS_WRAP_GET_U8(platform_class_wireless_get_rx_fastcharge_status, wls_get_rx_fastcharge_status);
WLS_WRAP_GET_INT(platform_class_wireless_get_ss_voltage, wls_get_ss_voltage);
WLS_WRAP_SET_U8(platform_class_wireless_do_renego, wls_do_renego);
WLS_WRAP_SET_BOOL(platform_class_wireless_set_parallel_charge, wls_set_parallel_charge);
WLS_WRAP_GET_INT(platform_class_wireless_get_vout_setted, wls_get_vout_setted);
WLS_WRAP_GET_U8(platform_class_wireless_get_poweroff_err_code, wls_get_poweroff_err_code);
WLS_WRAP_GET_U8(platform_class_wireless_get_rx_err_code, wls_get_rx_err_code);
WLS_WRAP_GET_U8(platform_class_wireless_get_tx_err_code, wls_get_tx_err_code);
WLS_WRAP_GET_INT(platform_class_wireless_get_project_vendor, wls_get_project_vendor);
WLS_WRAP_NOARG(platform_class_wireless_check_i2c_is_ok, wls_check_i2c_is_ok);
WLS_WRAP_SET_BOOL(platform_class_wireless_enable_rev_fod, wls_enable_rev_fod);
WLS_WRAP_SET_U8(platform_class_wireless_send_tx_q_value, wls_send_tx_q_value);
WLS_WRAP_SET_INT(platform_class_wireless_set_tx_fan_speed, wls_set_tx_fan_speed);
WLS_WRAP_GET_INT(platform_class_wireless_get_tx_fan_speed, wls_get_tx_fan_speed);
WLS_WRAP_SET_INT(platform_class_wireless_set_rx_offset, wls_set_rx_offset);
WLS_WRAP_GET_INT(platform_class_wireless_get_rx_offset, wls_get_rx_offset);
WLS_WRAP_SET_INT(platform_class_wireless_set_rx_sleep_mode, wls_set_rx_sleep_mode);
WLS_WRAP_NOARG(platform_class_wireless_download_fw_from_bin, wls_download_fw_from_bin);
WLS_WRAP_NOARG(platform_class_wireless_erase_fw, wls_erase_fw);
WLS_WRAP_GET_U8(platform_class_wireless_get_fw_version_check, wls_get_fw_version_check);
WLS_WRAP_NOARG(platform_class_wireless_download_fw, wls_download_fw);
WLS_WRAP_SET_U8(platform_class_wireless_process_factory_cmd, wls_process_factory_cmd);
WLS_WRAP_GET_BOOL(platform_class_wireless_get_hall_gpio_status, wls_get_hall_gpio_status);
WLS_WRAP_GET_BOOL(platform_class_wireless_get_magnetic_case_flag, wls_get_magnetic_case_flag);
WLS_WRAP_GET_BOOL(platform_class_wireless_check_firmware_state, wls_check_firmware_state);
WLS_WRAP_NOARG(platform_class_wireless_set_debug_fod_params, wls_set_debug_fod_params);
WLS_WRAP_SET_BOOL(platform_class_wireless_enable_vsys_ctrl, wls_enable_vsys_ctrl);
WLS_WRAP_GET_INT(platform_class_wireless_get_trx_isense, wls_get_trx_isense);
WLS_WRAP_GET_INT(platform_class_wireless_get_trx_vrect, wls_get_trx_vrect);
WLS_WRAP_GET_INT(platform_class_wireless_get_phone_case_category, wls_get_phone_case_category);
WLS_WRAP_SET_INT(platform_class_wireless_set_phone_case_category, wls_set_phone_case_category);
WLS_WRAP_SET_BOOL(platform_class_wireless_switch_bridge, wls_switch_bridge);
WLS_WRAP_GET_INT(platform_class_wireless_get_pen_hall3, wls_get_pen_hall3);
WLS_WRAP_GET_INT(platform_class_wireless_get_pen_hall3_s, wls_get_pen_hall3_s);
WLS_WRAP_GET_INT(platform_class_wireless_get_pen_hall4, wls_get_pen_hall4);
WLS_WRAP_GET_INT(platform_class_wireless_get_pen_hall4_s, wls_get_pen_hall4_s);
WLS_WRAP_GET_INT(platform_class_wireless_get_pen_hall_ppe_n, wls_get_pen_hall_ppe_n);
WLS_WRAP_GET_INT(platform_class_wireless_get_pen_hall_ppe_s, wls_get_pen_hall_ppe_s);
WLS_WRAP_GET_BOOL(platform_class_wireless_get_pen_full_flag, wls_get_pen_full_flag);
WLS_WRAP_GET_INT(platform_class_wireless_get_pen_place_err, wls_get_pen_place_err);
WLS_WRAP_SET_INT(platform_class_wireless_set_pen_place_err, wls_set_pen_place_err);
WLS_WRAP_GET_INT(platform_class_wireless_get_pen_soc, wls_get_pen_soc);
WLS_WRAP_GET_BOOL(platform_class_wireless_get_reverse_chg_en, wls_get_reverse_chg_en);
WLS_WRAP_SET_BOOL(platform_class_wireless_set_hboost_enable, wls_set_hboost_enable);
WLS_WRAP_SET_INT(platform_class_wireless_set_charge_type, wls_set_charge_type);
WLS_WRAP_SET_BOOL(platform_class_wireless_set_external_boost_enable, wls_set_external_boost_enable);
WLS_WRAP_GET_INT(platform_class_wireless_get_tx_iout, wls_get_tx_iout);
WLS_WRAP_GET_INT(platform_class_wireless_get_tx_vout, wls_get_tx_vout);
WLS_WRAP_GET_INT(platform_class_wireless_get_rx_brg_status, wls_get_rx_brg_status);

int platform_class_wireless_get_fw_upgrade_fail_info(unsigned int role, char **info)
{
	struct platform_wireless_class_ops_data *d = WLS_OP(role, wls_get_fw_upgrade_fail_info);
	return d ? d->ops->wls_get_fw_upgrade_fail_info(info, d->data) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(platform_class_wireless_get_fw_upgrade_fail_info);

int platform_class_wireless_get_fw_version(unsigned int role, char *buf)
{
	struct platform_wireless_class_ops_data *d = WLS_OP(role, wls_get_fw_version);
	return d ? d->ops->wls_get_fw_version(buf, d->data) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(platform_class_wireless_get_fw_version);

int platform_class_wireless_set_fw_bin(unsigned int role, const char *buf, int count)
{
	struct platform_wireless_class_ops_data *d = WLS_OP(role, wls_set_fw_bin);
	return d ? d->ops->wls_set_fw_bin(buf, count, d->data) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(platform_class_wireless_set_fw_bin);

int platform_class_wireless_get_tx_uuid(unsigned int role, u8 *uuid)
{
	struct platform_wireless_class_ops_data *d = WLS_OP(role, wls_get_tx_uuid);
	return d ? d->ops->wls_get_tx_uuid(uuid, d->data) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(platform_class_wireless_get_tx_uuid);

int platform_class_wireless_receive_transparent_data(unsigned int role,
		u8 *value, int buff_len, int *rcv_len)
{
	struct platform_wireless_class_ops_data *d = WLS_OP(role, wls_receive_transparent_data);
	return d ? d->ops->wls_receive_transparent_data(value, buff_len, rcv_len, d->data) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(platform_class_wireless_receive_transparent_data);

int platform_class_wireless_send_transparent_data(unsigned int role,
		u8 *data, u8 len)
{
	struct platform_wireless_class_ops_data *d = WLS_OP(role, wls_send_transparent_data);
	return d ? d->ops->wls_send_transparent_data(data, len, d->data) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(platform_class_wireless_send_transparent_data);

int platform_class_wireless_set_confirm_data(unsigned int role, u8 value)
{
	struct platform_wireless_class_ops_data *d = WLS_OP(role, wls_set_confirm_data);
	return d ? d->ops->wls_set_confirm_data(d->data, value) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(platform_class_wireless_set_confirm_data);

int platform_class_wireless_receive_test_cmd(unsigned int role, u8 *data, int *length)
{
	struct platform_wireless_class_ops_data *d = WLS_OP(role, wls_receive_test_cmd);
	return d ? d->ops->wls_receive_test_cmd(data, length, d->data) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(platform_class_wireless_receive_test_cmd);

int platform_class_wireless_set_debug_fod(unsigned int role, int *args, int count)
{
	struct platform_wireless_class_ops_data *d = WLS_OP(role, wls_set_debug_fod);
	return d ? d->ops->wls_set_debug_fod(args, count, d->data) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(platform_class_wireless_set_debug_fod);

int platform_class_wireless_get_debug_fod_type(unsigned int role,
		WLS_DEBUG_SET_FOD_TYPE *type)
{
	struct platform_wireless_class_ops_data *d = WLS_OP(role, wls_get_debug_fod_type);
	return d ? d->ops->wls_get_debug_fod_type(type, d->data) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(platform_class_wireless_get_debug_fod_type);

int platform_class_wireless_get_pen_mac(unsigned int role, u8 *mac)
{
	struct platform_wireless_class_ops_data *d = WLS_OP(role, wls_get_pen_mac);
	return d ? d->ops->wls_get_pen_mac(mac, d->data) : -EOPNOTSUPP;
}
EXPORT_SYMBOL(platform_class_wireless_get_pen_mac);

enum wireless_attr_id {
	WLS_ATTR_VOUT,
	WLS_ATTR_VRECT,
	WLS_ATTR_IOUT,
	WLS_ATTR_FW_VERSION,
	WLS_ATTR_SLEEP_RX,
	WLS_ATTR_TX_ADAPTER,
	WLS_ATTR_TEMP,
	WLS_ATTR_TX_SPEED,
	WLS_ATTR_RX_OFFSET,
	WLS_ATTR_TX_UUID,
};

static ssize_t wireless_sysfs_show(struct device *dev,
				   struct device_attribute *attr, char *buf);
static ssize_t wireless_sysfs_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count);

static struct mca_sysfs_attr_info wireless_sysfs_info[] = {
	mca_sysfs_attr_ro(wireless_sysfs, 0440, WLS_ATTR_VOUT, rx_vout),
	mca_sysfs_attr_ro(wireless_sysfs, 0440, WLS_ATTR_VRECT, rx_vrect),
	mca_sysfs_attr_ro(wireless_sysfs, 0440, WLS_ATTR_IOUT, rx_iout),
	mca_sysfs_attr_ro(wireless_sysfs, 0440, WLS_ATTR_FW_VERSION, fw_version),
	mca_sysfs_attr_rw(wireless_sysfs, 0660, WLS_ATTR_SLEEP_RX, sleep_rx),
	mca_sysfs_attr_ro(wireless_sysfs, 0440, WLS_ATTR_TX_ADAPTER, tx_adapter),
	mca_sysfs_attr_ro(wireless_sysfs, 0440, WLS_ATTR_TEMP, wls_die_temp),
	mca_sysfs_attr_rw(wireless_sysfs, 0660, WLS_ATTR_TX_SPEED, wls_tx_speed),
	mca_sysfs_attr_rw(wireless_sysfs, 0660, WLS_ATTR_RX_OFFSET, rx_offset),
	mca_sysfs_attr_ro(wireless_sysfs, 0440, WLS_ATTR_TX_UUID, tx_uuid),
};
static struct attribute *wireless_attrs[ARRAY_SIZE(wireless_sysfs_info) + 1];
static const struct attribute_group wireless_group = { .attrs = wireless_attrs };

static ssize_t wireless_sysfs_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *ai;
	int *role = dev_get_drvdata(dev);
	int val = 0, ret = 0;
	char fw[128] = { 0 };
	u8 uuid[4] = { 0 };

	if (!role)
		return -ENODEV;
	ai = mca_sysfs_lookup_attr(attr->attr.name, wireless_sysfs_info,
				  ARRAY_SIZE(wireless_sysfs_info));
	if (!ai)
		return -EINVAL;

	switch (ai->sysfs_attr_name) {
	case WLS_ATTR_VOUT: ret = platform_class_wireless_get_vout(*role, &val); break;
	case WLS_ATTR_VRECT: ret = platform_class_wireless_get_vrect(*role, &val); break;
	case WLS_ATTR_IOUT: ret = platform_class_wireless_get_iout(*role, &val); break;
	case WLS_ATTR_TX_ADAPTER: ret = platform_class_wireless_get_tx_adapter(*role, &val); break;
	case WLS_ATTR_TEMP: ret = platform_class_wireless_get_temp(*role, &val); break;
	case WLS_ATTR_TX_SPEED: ret = platform_class_wireless_get_tx_fan_speed(*role, &val); break;
	case WLS_ATTR_RX_OFFSET: ret = platform_class_wireless_get_rx_offset(*role, &val); break;
	case WLS_ATTR_FW_VERSION:
		ret = platform_class_wireless_get_fw_version(*role, fw);
		return ret ? ret : sysfs_emit(buf, "%s\n", fw);
	case WLS_ATTR_TX_UUID:
		ret = platform_class_wireless_get_tx_uuid(*role, uuid);
		return ret ? ret : sysfs_emit(buf, "%02x.%02x.%02x.%02x\n",
					      uuid[0], uuid[1], uuid[2], uuid[3]);
	default: return -EOPNOTSUPP;
	}
	return ret ? ret : sysfs_emit(buf, "%d\n", val);
}

static ssize_t wireless_sysfs_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *ai;
	int *role = dev_get_drvdata(dev);
	int val, ret;

	if (!role || kstrtoint(buf, 10, &val))
		return -EINVAL;
	ai = mca_sysfs_lookup_attr(attr->attr.name, wireless_sysfs_info,
				  ARRAY_SIZE(wireless_sysfs_info));
	if (!ai)
		return -EINVAL;
	switch (ai->sysfs_attr_name) {
	case WLS_ATTR_SLEEP_RX: ret = platform_class_wireless_set_enable_mode(*role, !!val); break;
	case WLS_ATTR_TX_SPEED: ret = platform_class_wireless_set_tx_fan_speed(*role, val); break;
	case WLS_ATTR_RX_OFFSET: ret = platform_class_wireless_set_rx_offset(*role, val); break;
	default: return -EACCES;
	}
	return ret ? ret : count;
}

static int platform_wireless_parse_dt(struct platform_wireless_dev *wls)
{
	struct device_node *node = wls->dev->of_node;
	int count, i, ret;

	ret = mca_parse_dts_u32(node, "wireless-num", &wls->wireless_num, 1);
	if (ret)
		return ret;
	if (wls->wireless_num < 1 || wls->wireless_num > MCA_WIRELESS_MAX_NUM)
		return -EINVAL;
	count = mca_parse_dts_count_strings(node, "wireless-dir-list",
					    MCA_WIRELESS_MAX_NUM, 1);
	if (count != wls->wireless_num)
		return -EINVAL;
	for (i = 0; i < count; i++) {
		ret = mca_parse_dts_string_index(node, "wireless-dir-list", i,
						 &wls->wireless_dir_list[i]);
		if (ret)
			return ret;
	}
	return 0;
}

static int platform_wireless_probe(struct platform_device *pdev)
{
	struct platform_wireless_dev *wls;
	int i, ret;

	wls = devm_kzalloc(&pdev->dev, sizeof(*wls), GFP_KERNEL);
	if (!wls)
		return -ENOMEM;
	wls->dev = &pdev->dev;
	platform_set_drvdata(pdev, wls);
	ret = platform_wireless_parse_dt(wls);
	if (ret)
		return ret;

	mca_sysfs_init_attrs(wireless_attrs, wireless_sysfs_info,
			     ARRAY_SIZE(wireless_sysfs_info));
	for (i = 0; i < wls->wireless_num; i++) {
		wls->wireless_dev_index[i] = i;
		wls->sysfs_dev[i] = mca_sysfs_create_group("xm_power",
			wls->wireless_dir_list[i], &wireless_group);
		if (!wls->sysfs_dev[i])
			return -ENOMEM;
		dev_set_drvdata(wls->sysfs_dev[i], &wls->wireless_dev_index[i]);
	}
	return 0;
}

static int platform_wireless_remove(struct platform_device *pdev)
{
	struct platform_wireless_dev *wls = platform_get_drvdata(pdev);
	int i;

	if (!wls)
		return 0;
	for (i = 0; i < wls->wireless_num; i++)
		if (wls->sysfs_dev[i])
			mca_sysfs_remove_group("xm_power", wls->sysfs_dev[i], &wireless_group);
	return 0;
}

static const struct of_device_id platform_wireless_match[] = {
	{ .compatible = "mca,platform_wireless" },
	{},
};
MODULE_DEVICE_TABLE(of, platform_wireless_match);

static struct platform_driver platform_wireless_driver = {
	.driver = {
		.name = "platform_wireless_class",
		.of_match_table = platform_wireless_match,
	},
	.probe = platform_wireless_probe,
	.remove = platform_wireless_remove,
};
module_platform_driver(platform_wireless_driver);

MODULE_DESCRIPTION("Xiaomi MCA wireless platform class");
MODULE_LICENSE("GPL v2");
