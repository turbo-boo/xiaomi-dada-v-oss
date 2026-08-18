// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi MCA charge-pump class dispatcher for Dada.
 *
 * This intentionally restores the hardware-facing ABI without the optional
 * sysfs presentation layer.  Dada charge-pump drivers register one ops table
 * per role (master/slave/third), and the charging policy accesses them through
 * these exported helpers.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <mca/platform/platform_cp_class.h>

struct mca_cp_ops_data {
	struct platform_class_cp_ops *ops;
	void *data;
};

static struct mca_cp_ops_data cp_data[CP_ROLE_MAX];

static struct mca_cp_ops_data *mca_cp_get(unsigned int role)
{
	if (role >= CP_ROLE_MAX || !cp_data[role].ops)
		return NULL;
	return &cp_data[role];
}

int platform_class_cp_register_ops(unsigned int role,
				   struct platform_class_cp_ops *ops, void *data)
{
	if (role >= CP_ROLE_MAX || !ops || !data)
		return -EINVAL;
	cp_data[role].ops = ops;
	cp_data[role].data = data;
	return 0;
}
EXPORT_SYMBOL(platform_class_cp_register_ops);

#define CP_WRAP_BOOL_IN(_fn, _op) \
int _fn(unsigned int role, bool val) \
{ \
	struct mca_cp_ops_data *d = mca_cp_get(role); \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(val, d->data); \
} \
EXPORT_SYMBOL(_fn)

#define CP_WRAP_INT_IN(_fn, _op) \
int _fn(unsigned int role, int val) \
{ \
	struct mca_cp_ops_data *d = mca_cp_get(role); \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(val, d->data); \
} \
EXPORT_SYMBOL(_fn)

#define CP_WRAP_BOOL_OUT(_fn, _op) \
int _fn(unsigned int role, bool *val) \
{ \
	struct mca_cp_ops_data *d = mca_cp_get(role); \
	if (!val) \
		return -EINVAL; \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(val, d->data); \
} \
EXPORT_SYMBOL(_fn)

#define CP_WRAP_INT_OUT(_fn, _op) \
int _fn(unsigned int role, int *val) \
{ \
	struct mca_cp_ops_data *d = mca_cp_get(role); \
	if (!val) \
		return -EINVAL; \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(val, d->data); \
} \
EXPORT_SYMBOL(_fn)

#define CP_WRAP_U32_AS_INT_OUT(_fn, _op) \
int _fn(unsigned int role, int *val) \
{ \
	struct mca_cp_ops_data *d = mca_cp_get(role); \
	if (!val) \
		return -EINVAL; \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op((u32 *)val, d->data); \
} \
EXPORT_SYMBOL(_fn)

#define CP_WRAP_NOARG(_fn, _op) \
int _fn(unsigned int role) \
{ \
	struct mca_cp_ops_data *d = mca_cp_get(role); \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(d->data); \
} \
EXPORT_SYMBOL(_fn)

CP_WRAP_BOOL_IN(platform_class_cp_set_charging_enable, cp_set_enable);
CP_WRAP_BOOL_OUT(platform_class_cp_get_charging_enabled, cp_get_enabled);
CP_WRAP_BOOL_IN(platform_class_cp_set_present, cp_set_present);
CP_WRAP_BOOL_IN(platform_class_cp_set_revchg, cp_set_revchg);
CP_WRAP_INT_IN(platform_class_cp_set_adjustadble_timeout,
	       cp_set_adjustadble_timeout);
CP_WRAP_BOOL_OUT(platform_class_cp_get_vbus_present, cp_get_vbus_present);
CP_WRAP_BOOL_OUT(platform_class_cp_get_present, cp_get_present);
CP_WRAP_BOOL_OUT(platform_class_cp_get_battery_present, cp_get_battery_present);
CP_WRAP_U32_AS_INT_OUT(platform_class_cp_get_battery_voltage,
		       cp_get_battery_voltage);
CP_WRAP_U32_AS_INT_OUT(platform_class_cp_get_battery_current,
		       cp_get_battery_current);
CP_WRAP_U32_AS_INT_OUT(platform_class_cp_get_battery_temperature,
		       cp_get_battery_temperature);
CP_WRAP_U32_AS_INT_OUT(platform_class_cp_get_bus_voltage, cp_get_bus_voltage);
CP_WRAP_U32_AS_INT_OUT(platform_class_cp_get_bus_current, cp_get_bus_current);
CP_WRAP_INT_OUT(platform_class_cp_get_bus_temperature, cp_get_bus_temperature);
CP_WRAP_INT_OUT(platform_class_cp_get_die_temperature, cp_get_die_temperature);
CP_WRAP_INT_OUT(platform_class_cp_get_alarm_status, cp_get_alarm_status);
CP_WRAP_INT_OUT(platform_class_cp_get_fault_status, cp_get_fault_status);
CP_WRAP_INT_OUT(platform_class_cp_get_bus_error_status, cp_get_bus_error_status);
CP_WRAP_INT_OUT(platform_class_cp_get_reg_status, cp_get_reg_status);
CP_WRAP_INT_IN(platform_class_cp_set_mode, cp_set_mode);
CP_WRAP_INT_OUT(platform_class_cp_get_mode, cp_get_mode);
CP_WRAP_INT_IN(platform_class_cp_device_init, cp_device_init);
CP_WRAP_BOOL_IN(platform_class_cp_enable_adc, cp_enable_adc);
CP_WRAP_BOOL_OUT(platform_class_cp_get_bypass_support, cp_get_bypass_support);
CP_WRAP_NOARG(platform_class_cp_dump_register, cp_dump_register);
CP_WRAP_INT_OUT(platform_class_cp_get_chip_vendor, cp_get_chip_vendor);
CP_WRAP_NOARG(platform_class_cp_get_probe_ok, cp_get_probe_ok);
CP_WRAP_BOOL_IN(platform_class_cp_enable_acdrv_manual, cp_enable_acdrv_manual);
CP_WRAP_BOOL_IN(platform_class_cp_enable_wpcgate, cp_enable_wpcgate);
CP_WRAP_BOOL_IN(platform_class_cp_enable_ovpgate, cp_enable_ovpgate);
CP_WRAP_BOOL_OUT(platform_class_cp_get_ovpgate_status, cp_get_ovpgate_status);
CP_WRAP_U32_AS_INT_OUT(platform_class_cp_get_usb_voltage, cp_get_usb_voltage);
CP_WRAP_INT_OUT(platform_class_cp_get_errorhl_stat, cp_get_errorhl_stat);
CP_WRAP_BOOL_IN(platform_class_cp_enable_busucp, cp_enable_busucp);
CP_WRAP_BOOL_OUT(platform_class_cp_get_adc_enabled, cp_get_adc_enabled);
CP_WRAP_INT_IN(platform_class_cp_set_busovp, cp_set_busovp);
CP_WRAP_BOOL_IN(platform_class_cp_enable_vbus_errorhi, cp_enable_vbus_errorhi);
CP_WRAP_BOOL_IN(platform_class_cp_enable_vbus_errorlo, cp_enable_vbus_errorlo);
CP_WRAP_BOOL_IN(platform_class_cp_set_manual_revchg_mode,
		cp_set_manual_revchg_mode);
CP_WRAP_BOOL_IN(platform_class_cp_set_cp_reverse_mode,
		cp_set_cp_reverse_mode);
CP_WRAP_INT_IN(platform_class_cp_set_fsw, cp_set_fsw);
CP_WRAP_INT_OUT(platform_class_cp_get_fsw, cp_get_fsw);
CP_WRAP_INT_OUT(platform_class_cp_get_fsw_step, cp_get_fsw_step);
CP_WRAP_INT_OUT(platform_class_cp_get_tdie, cp_get_tdie);
CP_WRAP_BOOL_IN(platform_class_cp_set_qb, cp_set_qb);
CP_WRAP_BOOL_IN(platform_class_cp_set_rcp, cp_set_rcp);
CP_WRAP_INT_IN(platform_class_cp_set_pmid2outuvp_th, cp_set_pmid2outuvp_th);

int platform_class_cp_get_int_stat(unsigned int role, int channel, bool *result)
{
	struct mca_cp_ops_data *d = mca_cp_get(role);

	if (!result)
		return -EINVAL;
	if (!d || !d->ops->cp_get_int_stat)
		return -EOPNOTSUPP;
	return d->ops->cp_get_int_stat(channel, result, d->data);
}
EXPORT_SYMBOL(platform_class_cp_get_int_stat);

int platform_class_cp_enable_ovpgate_with_check(unsigned int role,
						int type_temp, bool en)
{
	struct mca_cp_ops_data *d = mca_cp_get(role);

	if (!d || !d->ops->cp_enable_ovpgate_with_check)
		return -EOPNOTSUPP;
	return d->ops->cp_enable_ovpgate_with_check(type_temp, en, d->data);
}
EXPORT_SYMBOL(platform_class_cp_enable_ovpgate_with_check);

int platform_class_cp_get_ibus_delta(int *val)
{
	int master = 0, slave = 0;
	int ret_master, ret_slave;

	if (!val)
		return -EINVAL;
	ret_master = platform_class_cp_get_bus_current(CP_ROLE_MASTER, &master);
	ret_slave = platform_class_cp_get_bus_current(CP_ROLE_SLAVE, &slave);
	if (ret_master)
		master = 0;
	if (ret_slave)
		slave = 0;
	*val = abs(master - slave);
	return ret_master ? ret_master : ret_slave;
}
EXPORT_SYMBOL(platform_class_cp_get_ibus_delta);

int platform_class_cp_get_ibus_total(int *val)
{
	int master = 0, slave = 0;
	int ret_master, ret_slave;

	if (!val)
		return -EINVAL;
	ret_master = platform_class_cp_get_bus_current(CP_ROLE_MASTER, &master);
	ret_slave = platform_class_cp_get_bus_current(CP_ROLE_SLAVE, &slave);
	if (ret_master)
		master = 0;
	if (ret_slave)
		slave = 0;
	*val = master + slave;
	return ret_master ? ret_master : ret_slave;
}
EXPORT_SYMBOL(platform_class_cp_get_ibus_total);

int platform_class_cp_get_battery_vout(unsigned int role, u32 *val)
{
	struct mca_cp_ops_data *d = mca_cp_get(role);

	if (!val)
		return -EINVAL;
	if (!d || !d->ops->cp_get_battery_vout)
		return -EOPNOTSUPP;
	return d->ops->cp_get_battery_vout(val, d->data);
}
EXPORT_SYMBOL(platform_class_cp_get_battery_vout);

int platform_class_cp_set_default_fsw(unsigned int role)
{
	struct mca_cp_ops_data *d = mca_cp_get(role);

	if (!d || !d->ops->cp_set_default_fsw)
		return -EOPNOTSUPP;
	return d->ops->cp_set_default_fsw(d->data);
}
EXPORT_SYMBOL(platform_class_cp_set_default_fsw);

MODULE_DESCRIPTION("Xiaomi MCA charge-pump class dispatcher");
MODULE_LICENSE("GPL v2");
