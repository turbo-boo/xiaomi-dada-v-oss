// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/module.h>
#include <mca/platform/platform_buckchg_class.h>

struct mca_buckchg_data {
	struct platform_class_buckchg_ops *ops;
	void *data;
};

static struct mca_buckchg_data buckchg_data[MAX_BUCK_CHARGER];

static struct mca_buckchg_data *mca_buckchg_get(unsigned int role)
{
	if (role >= MAX_BUCK_CHARGER || !buckchg_data[role].ops)
		return NULL;
	return &buckchg_data[role];
}

int platform_class_buckchg_ops_register(unsigned int role, void *data,
					struct platform_class_buckchg_ops *ops)
{
	if (role >= MAX_BUCK_CHARGER || !ops)
		return -EINVAL;
	buckchg_data[role].ops = ops;
	buckchg_data[role].data = data;
	return 0;
}
EXPORT_SYMBOL(platform_class_buckchg_ops_register);

#define BUCK_INT_IN(_fn, _op) \
int _fn(unsigned int role, int value) \
{ \
	struct mca_buckchg_data *d = mca_buckchg_get(role); \
	if (!d || !d->ops->_op) return -EOPNOTSUPP; \
	return d->ops->_op(d->data, value); \
} \
EXPORT_SYMBOL(_fn)

#define BUCK_BOOL_IN(_fn, _op) \
int _fn(unsigned int role, bool value) \
{ \
	struct mca_buckchg_data *d = mca_buckchg_get(role); \
	if (!d || !d->ops->_op) return -EOPNOTSUPP; \
	return d->ops->_op(d->data, value); \
} \
EXPORT_SYMBOL(_fn)

#define BUCK_INT_OUT(_fn, _op) \
int _fn(unsigned int role, int *value) \
{ \
	struct mca_buckchg_data *d = mca_buckchg_get(role); \
	if (!value) return -EINVAL; \
	if (!d || !d->ops->_op) return -EOPNOTSUPP; \
	return d->ops->_op(d->data, value); \
} \
EXPORT_SYMBOL(_fn)

#define BUCK_BOOL_OUT(_fn, _op) \
int _fn(unsigned int role, bool *value) \
{ \
	struct mca_buckchg_data *d = mca_buckchg_get(role); \
	if (!value) return -EINVAL; \
	if (!d || !d->ops->_op) return -EOPNOTSUPP; \
	return d->ops->_op(d->data, value); \
} \
EXPORT_SYMBOL(_fn)

#define BUCK_NOARG(_fn, _op) \
int _fn(unsigned int role) \
{ \
	struct mca_buckchg_data *d = mca_buckchg_get(role); \
	if (!d || !d->ops->_op) return -EOPNOTSUPP; \
	return d->ops->_op(d->data); \
} \
EXPORT_SYMBOL(_fn)

BUCK_INT_IN(platform_class_buckchg_ops_enable_hvdcp, enable_hvdcp);
BUCK_INT_OUT(platform_class_buckchg_ops_get_online, get_online);
BUCK_BOOL_OUT(platform_class_buckchg_ops_is_charge_done, is_charge_done);
BUCK_INT_OUT(platform_class_buckchg_ops_get_hiz_status, get_hiz_status);
BUCK_INT_OUT(platform_class_buckchg_ops_get_input_volt_lmt, get_input_volt_lmt);
BUCK_INT_OUT(platform_class_buckchg_ops_get_input_curr_lmt, get_input_curr_lmt);
BUCK_INT_OUT(platform_class_buckchg_ops_get_bus_curr, get_bus_curr);
BUCK_INT_OUT(platform_class_buckchg_ops_get_bus_volt, get_bus_volt);
BUCK_INT_OUT(platform_class_buckchg_ops_get_usb_sns_volt, get_usb_sns_volt);
BUCK_INT_OUT(platform_class_buckchg_ops_get_ac_volt, get_ac_volt);
BUCK_INT_OUT(platform_class_buckchg_ops_get_batt_volt_sns, get_batt_volt_sns);
BUCK_INT_OUT(platform_class_buckchg_ops_get_batt_volt, get_batt_volt);
BUCK_INT_OUT(platform_class_buckchg_ops_get_batt_curr, get_batt_curr);
BUCK_INT_OUT(platform_class_buckchg_ops_get_sys_volt, get_sys_volt);
BUCK_INT_OUT(platform_class_buckchg_ops_get_bus_tsns, get_bus_tsns);
BUCK_INT_OUT(platform_class_buckchg_ops_get_batt_tsns, get_batt_tsns);
BUCK_INT_OUT(platform_class_buckchg_ops_get_die_temp, get_die_temp);
BUCK_INT_OUT(platform_class_buckchg_ops_get_batt_id, get_batt_id);
BUCK_INT_OUT(platform_class_buckchg_ops_get_chg_status, get_chg_status);
BUCK_INT_OUT(platform_class_buckchg_ops_get_chg_type, get_chg_type);
BUCK_INT_OUT(platform_class_buckchg_ops_get_term_curr, get_term_curr);
BUCK_INT_OUT(platform_class_buckchg_ops_get_term_volt, get_term_volt);
BUCK_INT_OUT(platform_class_buckchg_ops_get_wls_curr, get_wls_curr);
BUCK_BOOL_IN(platform_class_buckchg_ops_set_hiz, set_hiz);
BUCK_BOOL_IN(platform_class_buckchg_ops_set_wls_hiz, set_wls_hiz);
BUCK_INT_IN(platform_class_buckchg_ops_set_input_curr_lmt, set_input_curr_lmt);
BUCK_INT_IN(platform_class_buckchg_ops_set_wls_input_curr_lmt, set_wls_input_curr_lmt);
BUCK_INT_IN(platform_class_buckchg_ops_set_input_volt_lmt, set_input_volt_lmt);
BUCK_INT_IN(platform_class_buckchg_ops_set_ichg, set_ichg);
BUCK_BOOL_IN(platform_class_buckchg_ops_set_chg, set_chg);
BUCK_INT_IN(platform_class_buckchg_ops_set_buck_fsw, set_buck_fsw);
BUCK_INT_IN(platform_class_buckchg_ops_set_otg_curr, set_otg_curr);
BUCK_INT_IN(platform_class_buckchg_ops_set_otg_volt, set_otg_volt);
BUCK_BOOL_IN(platform_class_buckchg_ops_set_term, set_term);
BUCK_INT_IN(platform_class_buckchg_ops_set_term_curr, set_term_curr);
BUCK_INT_IN(platform_class_buckchg_ops_set_term_volt, set_term_volt);
BUCK_BOOL_IN(platform_class_buckchg_ops_adc_enable, adc_enable);
BUCK_INT_IN(platform_class_buckchg_ops_set_prechg_volt, set_prechg_volt);
BUCK_INT_IN(platform_class_buckchg_ops_set_prechg_curr, set_prechg_curr);
BUCK_INT_IN(platform_class_buckchg_ops_force_dpdm, force_dpdm);
BUCK_BOOL_IN(platform_class_buckchg_ops_request_dpdm, request_dpdm);
BUCK_INT_IN(platform_class_buckchg_ops_set_wd_timeout, set_wd_timeout);
BUCK_NOARG(platform_class_buckchg_ops_kick_wd, kick_wd);
BUCK_INT_IN(platform_class_buckchg_ops_set_qc_volt, set_qc_volt);
BUCK_INT_IN(platform_class_buckchg_ops_set_usb_aicl_cont_thd, set_usb_aicl_cont_thd);
BUCK_INT_OUT(platform_class_buckchg_ops_get_usb_aicl_cont_thd, get_usb_aicl_cont_thd);
BUCK_INT_IN(platform_class_buckchg_ops_set_opt_fws, set_opt_fws);
BUCK_BOOL_IN(platform_class_buckchg_ops_usb_adapter_allow_override, usb_adapter_allow_override);
BUCK_INT_IN(platform_class_buckchg_ops_set_qc3_volt, set_qc3_volt);
BUCK_INT_OUT(platform_class_buckchg_ops_get_otg_boost_src, get_otg_boost_src);
BUCK_INT_OUT(platform_class_buckchg_ops_get_otg_boost_enable_status, get_otg_boost_enable_status);
BUCK_INT_OUT(platform_class_buckchg_ops_get_otg_gate_enable_status, get_otg_gate_enable_status);
BUCK_INT_IN(platform_class_buckchg_ops_set_otg, set_otg);
BUCK_INT_IN(platform_class_buckchg_ops_set_boost_enable, set_boost_enable);
BUCK_INT_IN(platform_class_buckchg_ops_set_boost_voltage, set_boost_voltage);
BUCK_BOOL_IN(platform_class_buckchg_ops_set_aicl_enable, set_aicl_enable);
BUCK_BOOL_IN(platform_class_buckchg_ops_set_rerun_aicl, set_rerun_aicl);
BUCK_BOOL_OUT(platform_class_buckchg_ops_is_support_cid, is_support_cid);
BUCK_BOOL_IN(platform_class_buckchg_ops_set_ship_mode, set_ship_mode);
BUCK_BOOL_OUT(platform_class_buckchg_ops_get_ship_mode, get_ship_mode);
BUCK_BOOL_IN(platform_class_buckchg_ops_set_wls_vdd_flag, set_wls_vdd_flag);
BUCK_INT_OUT(platform_class_buckchg_ops_get_lpd_enable, get_lpd_enable);
BUCK_INT_OUT(platform_class_buckchg_ops_get_lpd_status, get_lpd_status);
BUCK_INT_OUT(platform_class_buckchg_ops_get_lpd_sbu1, get_lpd_sbu1);
BUCK_INT_OUT(platform_class_buckchg_ops_get_lpd_sbu2, get_lpd_sbu2);
BUCK_INT_OUT(platform_class_buckchg_ops_get_lpd_cc1, get_lpd_cc1);
BUCK_INT_OUT(platform_class_buckchg_ops_get_lpd_cc2, get_lpd_cc2);
BUCK_INT_OUT(platform_class_buckchg_ops_get_lpd_dp, get_lpd_dp);
BUCK_INT_OUT(platform_class_buckchg_ops_get_lpd_dm, get_lpd_dm);
BUCK_INT_IN(platform_class_buckchg_ops_set_lpd_sbu1, set_lpd_sbu1);
BUCK_INT_IN(platform_class_buckchg_ops_set_lpd_control, set_lpd_control);
BUCK_INT_OUT(platform_class_buckchg_ops_get_lpd_control, get_lpd_control);
BUCK_INT_IN(platform_class_buckchg_ops_set_lpd_uart_control, set_lpd_uart_control);
BUCK_INT_OUT(platform_class_buckchg_ops_get_lpd_uart_control, get_lpd_uart_control);
BUCK_INT_OUT(platform_class_buckchg_ops_get_pack_vbat, get_pack_vbat);
BUCK_BOOL_IN(platform_class_buckchg_ops_set_eu_model, set_eu_model);
BUCK_BOOL_IN(platform_class_buckchg_ops_set_restart_aicl, set_restart_aicl);
BUCK_INT_OUT(platform_class_buckchg_ops_get_pack_ibat, get_pack_ibat);
BUCK_INT_OUT(platform_class_buckchg_ops_get_pack_tbat, get_pack_tbat);
BUCK_INT_OUT(platform_class_buckchg_ops_get_aicl_status, get_aicl_status);
BUCK_INT_IN(platform_class_buckchg_ops_set_too_hot_limit, set_too_hot_limit);

int platform_class_buckchg_ops_is_init_ok(unsigned int role)
{
	struct mca_buckchg_data *d = mca_buckchg_get(role);
	if (!d || !d->ops->is_init_ok)
		return -EOPNOTSUPP;
	return d->ops->is_init_ok(d->data);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_is_init_ok);

MODULE_DESCRIPTION("Xiaomi MCA buck charger class dispatcher");
MODULE_LICENSE("GPL v2");
