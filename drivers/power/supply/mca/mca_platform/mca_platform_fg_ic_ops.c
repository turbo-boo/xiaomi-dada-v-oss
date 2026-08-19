// SPDX-License-Identifier: GPL-2.0
/* Xiaomi MCA fuel-gauge ops dispatcher for Dada. */
#include <linux/errno.h>
#include <linux/module.h>
#include <mca/platform/platform_fg_ic_ops.h>

struct mca_fg_ops_data {
	struct fuelguage_ic_ops *ops;
	void *data;
};

static struct mca_fg_ops_data fg_data[FG_IC_MAX];

static struct mca_fg_ops_data *mca_fg_get(unsigned int role)
{
	if (role >= FG_IC_MAX || !fg_data[role].ops)
		return NULL;
	return &fg_data[role];
}

int platform_fg_ic_ops_register(unsigned int role, void *data,
				struct fuelguage_ic_ops *ops)
{
	if (role >= FG_IC_MAX || !data || !ops)
		return -EINVAL;
	fg_data[role].data = data;
	fg_data[role].ops = ops;
	return 0;
}
EXPORT_SYMBOL(platform_fg_ic_ops_register);

#define FG_INT_OUT(_fn, _op) \
int _fn(unsigned int role, int *val) \
{ \
	struct mca_fg_ops_data *d = mca_fg_get(role); \
	if (!val) \
		return -EINVAL; \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(d->data, val); \
} \
EXPORT_SYMBOL(_fn)

#define FG_BOOL_OUT(_fn, _op) \
int _fn(unsigned int role, bool *val) \
{ \
	struct mca_fg_ops_data *d = mca_fg_get(role); \
	if (!val) \
		return -EINVAL; \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(d->data, val); \
} \
EXPORT_SYMBOL(_fn)

#define FG_INT_IN(_fn, _op) \
int _fn(unsigned int role, int val) \
{ \
	struct mca_fg_ops_data *d = mca_fg_get(role); \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(d->data, val); \
} \
EXPORT_SYMBOL(_fn)

#define FG_BOOL_IN(_fn, _op) \
int _fn(unsigned int role, bool val) \
{ \
	struct mca_fg_ops_data *d = mca_fg_get(role); \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(d->data, val); \
} \
EXPORT_SYMBOL(_fn)

#define FG_NOARG_RET(_fn, _op) \
int _fn(unsigned int role) \
{ \
	struct mca_fg_ops_data *d = mca_fg_get(role); \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(d->data); \
} \
EXPORT_SYMBOL(_fn)

FG_BOOL_OUT(platform_fg_ops_probe_ok, fg_ic_probe_ok);
FG_NOARG_RET(platform_fg_ops_get_soc, fg_ic_get_soc);
FG_INT_OUT(platform_fg_ops_get_rsoc, fg_ic_get_rsoc);
FG_INT_OUT(platform_fg_ops_get_curr, fg_ic_get_curr);
FG_INT_IN(platform_fg_ops_set_authentic, fg_ic_set_authentic);
FG_INT_OUT(platform_fg_ops_get_authentic, fg_ic_get_authentic);
FG_BOOL_OUT(platform_fg_ops_get_error_state, fg_ic_get_error_state);
FG_INT_OUT(platform_fg_ops_get_volt, fg_ic_get_volt);
FG_INT_IN(platform_fg_ops_set_temp, fg_ic_set_temp);
FG_INT_OUT(platform_fg_ops_get_temp, fg_ic_get_temp);
FG_INT_IN(platform_fg_ops_set_iterm, fg_ic_set_iterm);
FG_NOARG_RET(platform_fg_ops_get_charge_status, fg_ic_get_charge_status);
FG_INT_OUT(platform_fg_ops_get_rm, fg_ic_get_rm);
FG_INT_OUT(platform_fg_ops_get_isc_alert_level, fg_ic_get_isc_alert_level);
FG_INT_OUT(platform_fg_ops_get_soa_alert_level, fg_ic_get_soa_alert_level);
FG_INT_OUT(platform_fg_ops_get_fastcharge, fg_ic_get_fastcharge);
FG_BOOL_IN(platform_fg_ops_set_fastcharge, fg_ic_set_fastcharge);
FG_INT_OUT(platform_fg_ops_get_chg_vol, fg_ic_get_chg_vol);
FG_INT_OUT(platform_fg_ops_get_chip_ok, fg_ic_get_chip_ok);
FG_INT_OUT(platform_fg_ops_get_cyclecount, fg_ic_get_cyclecount);
FG_INT_OUT(platform_fg_ops_get_tte, fg_ic_get_tte);
FG_INT_OUT(platform_fg_ops_get_ttf, fg_ic_get_ttf);
FG_INT_OUT(platform_fg_ops_get_fcc, fg_ic_get_fcc);
FG_INT_OUT(platform_fg_ops_get_full_design, fg_ic_get_full_design);
FG_INT_OUT(platform_fg_ops_get_decimal_rate, fg_ic_get_decimal_rate);
FG_INT_OUT(platform_fg_ops_get_decimal, fg_ic_get_decimal);
FG_INT_OUT(platform_fg_ops_get_soh, fg_ic_get_soh);
FG_INT_OUT(platform_fg_ops_get_temp_max, fg_ic_get_temp_max);
FG_INT_OUT(platform_fg_ops_get_time_ot, fg_ic_get_time_ot);
FG_INT_OUT(platform_fg_ops_get_cutoff_voltage, fg_ic_get_cutoff_voltage);
FG_INT_IN(platform_fg_ops_set_cutoff_voltage, fg_ic_set_cutoff_voltage);
FG_NOARG_RET(platform_fg_ops_get_dod_count, fg_ic_get_dod_count);
FG_INT_OUT(platform_fg_ops_get_count_level1, fg_ic_get_count_level1);
FG_INT_OUT(platform_fg_ops_get_count_level2, fg_ic_get_count_level2);
FG_INT_OUT(platform_fg_ops_get_count_level3, fg_ic_get_count_level3);
FG_INT_OUT(platform_fg_ops_get_count_lowtemp, fg_ic_get_count_lowtemp);
FG_NOARG_RET(platform_fg_ops_set_clear_count_data, fg_ic_set_clear_count_data);
FG_INT_OUT(platform_fg_ops_get_adapt_power, fg_ic_get_adapt_power);
FG_INT_OUT(platform_fg_ops_get_aged_flag, fg_ic_get_aged_flag);
FG_INT_OUT(platform_fg_ops_get_raw_soc, fg_ic_get_raw_soc);
FG_INT_OUT(platform_fg_ops_get_real_supplement_energy,
	   fg_ic_get_real_supplement_energy);
FG_INT_OUT(platform_fg_ops_get_calibration_ffc_iterm,
	   fg_ic_get_calibration_ffc_iterm);
FG_INT_OUT(platform_fg_ops_get_calibration_charge_energy,
	   fg_ic_get_calibration_charge_energy);
FG_INT_OUT(platform_fg_ops_get_temp_min, fg_ic_get_temp_min);
FG_BOOL_OUT(platform_fg_ops_get_fc, fg_ic_get_fc);
FG_BOOL_IN(platform_fg_ops_set_co, fg_ic_set_co);
FG_INT_OUT(platform_fg_ops_get_pack_vendor, fg_ic_get_pack_vendor);
FG_INT_OUT(platform_fg_ops_get_original_temp, fg_ic_get_original_temp);
FG_INT_OUT(platform_fg_ops_get_average_current, fg_ic_get_average_current);
FG_INT_OUT(platform_fg_ops_get_ota_update_flag, fg_ic_get_ota_update_flag);
FG_NOARG_RET(platform_fg_ops_ota_update_check, fg_ic_ota_update_check);

int platform_fg_ops_get_batt_info(unsigned int role, void *info)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (!info)
		return -EINVAL;
	if (!d || !d->ops->fg_ic_get_batt_info)
		return -EOPNOTSUPP;
	return d->ops->fg_ic_get_batt_info(d->data, info);
}
EXPORT_SYMBOL(platform_fg_ops_get_batt_info);

int platform_fg_ops_set_verify_digest(unsigned int role, char *buf)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (!buf)
		return -EINVAL;
	if (!d || !d->ops->fg_ic_set_verify_digest)
		return -EOPNOTSUPP;
	return d->ops->fg_ic_set_verify_digest(d->data, buf);
}
EXPORT_SYMBOL(platform_fg_ops_set_verify_digest);

int platform_fg_ops_get_verify_digest(unsigned int role, char *buf)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (!buf)
		return -EINVAL;
	if (!d || !d->ops->fg_ic_get_verify_digest)
		return -EOPNOTSUPP;
	return d->ops->fg_ic_get_verify_digest(d->data, buf);
}
EXPORT_SYMBOL(platform_fg_ops_get_verify_digest);

int platform_fg_ops_get_batt_cell_info(unsigned int role, const char **name)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (!name)
		return -EINVAL;
	if (!d || !d->ops->fg_ic_get_batt_cell_info)
		return -EOPNOTSUPP;
	return d->ops->fg_ic_get_batt_cell_info(d->data, name);
}
EXPORT_SYMBOL(platform_fg_ops_get_batt_cell_info);

void platform_fg_ops_fl4p0_enable_check(unsigned int role)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (d && d->ops->fg_ic_fl4p0_enable_check)
		d->ops->fg_ic_fl4p0_enable_check(d->data);
}
EXPORT_SYMBOL(platform_fg_ops_fl4p0_enable_check);

void platform_fg_ops_update_fw(unsigned int role)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (d && d->ops->fg_ic_update_fw)
		d->ops->fg_ic_update_fw(d->data);
}
EXPORT_SYMBOL(platform_fg_ops_update_fw);

int platform_fg_ops_get_device_name(unsigned int role, const char **name)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (!name)
		return -EINVAL;
	if (!d || !d->ops->fg_ic_get_device_name)
		return -EOPNOTSUPP;
	return d->ops->fg_ic_get_device_name(d->data, name);
}
EXPORT_SYMBOL(platform_fg_ops_get_device_name);

void platform_fg_ops_set_force_report_full(unsigned int role)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (d && d->ops->fg_ic_set_force_report_full)
		d->ops->fg_ic_set_force_report_full(d->data);
}
EXPORT_SYMBOL(platform_fg_ops_set_force_report_full);

void platform_fg_ops_get_ui_soh(unsigned int role, int *ui_soh)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (ui_soh && d && d->ops->fg_ic_get_ui_soh)
		d->ops->fg_ic_get_ui_soh(d->data, ui_soh);
}
EXPORT_SYMBOL(platform_fg_ops_get_ui_soh);

unsigned long platform_fg_ops_get_calc_rvalue(unsigned int role)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (!d || !d->ops->fg_ic_get_calc_rvalue)
		return 0;
	return d->ops->fg_ic_get_calc_rvalue(d->data);
}
EXPORT_SYMBOL(platform_fg_ops_get_calc_rvalue);

void platform_fg_ops_get_batt_abnormal_info(unsigned int role, int *info)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (info && d && d->ops->fg_ic_get_batt_abnormal_info)
		d->ops->fg_ic_get_batt_abnormal_info(d->data, info);
}
EXPORT_SYMBOL(platform_fg_ops_get_batt_abnormal_info);

int platform_fg_ops_get_first_usage_date(unsigned int role, u8 *buf)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (!buf)
		return -EINVAL;
	if (!d || !d->ops->fg_ic_get_first_usage_date)
		return -EOPNOTSUPP;
	return d->ops->fg_ic_get_first_usage_date(d->data, buf);
}
EXPORT_SYMBOL(platform_fg_ops_get_first_usage_date);

int platform_fg_ops_get_manufacturing_date(unsigned int role, u8 *buf)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (!buf)
		return -EINVAL;
	if (!d || !d->ops->fg_ic_get_manufacturing_date)
		return -EOPNOTSUPP;
	return d->ops->fg_ic_get_manufacturing_date(d->data, buf);
}
EXPORT_SYMBOL(platform_fg_ops_get_manufacturing_date);

void platform_fg_ops_set_first_usage_date(unsigned int role, const char *date)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (date && d && d->ops->fg_ic_set_first_usage_date)
		d->ops->fg_ic_set_first_usage_date(d->data, date);
}
EXPORT_SYMBOL(platform_fg_ops_set_first_usage_date);

void platform_fg_ops_qbg_send_chg_data(unsigned int role)
{
	struct mca_fg_ops_data *d = mca_fg_get(role);
	if (d && d->ops->fg_ic_qbg_send_chg_data)
		d->ops->fg_ic_qbg_send_chg_data(d->data);
}
EXPORT_SYMBOL(platform_fg_ops_qbg_send_chg_data);

MODULE_DESCRIPTION("Xiaomi MCA fuel-gauge ops dispatcher");
MODULE_LICENSE("GPL v2");
