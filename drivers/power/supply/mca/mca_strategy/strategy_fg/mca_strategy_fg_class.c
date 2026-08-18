// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <mca/strategy/strategy_fg_class.h>

static DEFINE_MUTEX(fg_class_lock);
static void *fg_class_data;
static struct strategy_fg_class_ops *fg_class_ops;

int strategy_class_fg_ops_register(void *data, struct strategy_fg_class_ops *ops)
{
	if (!ops)
		return -EINVAL;
	mutex_lock(&fg_class_lock);
	fg_class_data = data;
	fg_class_ops = ops;
	mutex_unlock(&fg_class_lock);
	return 0;
}
EXPORT_SYMBOL(strategy_class_fg_ops_register);

#define FG_CLASS_NOARG(_fn, _op) \
int _fn(void) \
{ \
	struct strategy_fg_class_ops *ops = READ_ONCE(fg_class_ops); \
	if (!ops || !ops->_op) \
		return -EOPNOTSUPP; \
	return ops->_op(READ_ONCE(fg_class_data)); \
} \
EXPORT_SYMBOL(_fn)

#define FG_CLASS_INT_OUT(_fn, _op) \
int _fn(int *value) \
{ \
	struct strategy_fg_class_ops *ops = READ_ONCE(fg_class_ops); \
	if (!value) \
		return -EINVAL; \
	if (!ops || !ops->_op) \
		return -EOPNOTSUPP; \
	return ops->_op(READ_ONCE(fg_class_data), value); \
} \
EXPORT_SYMBOL(_fn)

FG_CLASS_NOARG(strategy_class_fg_ops_is_init_ok, strategy_fg_is_init_ok);
FG_CLASS_INT_OUT(strategy_class_fg_ops_get_rsoc, strategy_fg_get_rsoc);
FG_CLASS_NOARG(strategy_class_fg_ops_get_soc, strategy_fg_get_soc);
FG_CLASS_INT_OUT(strategy_class_fg_ops_get_temperature, strategy_fg_get_temp);
FG_CLASS_INT_OUT(strategy_class_fg_ops_get_current, strategy_fg_get_current);
FG_CLASS_INT_OUT(strategy_class_fg_ops_get_voltage, strategy_fg_get_voltage);
FG_CLASS_INT_OUT(strategy_class_fg_ops_get_cyclecount, strategy_fg_get_cycle);
FG_CLASS_INT_OUT(strategy_class_fg_get_voltage_mean, strategy_fg_get_voltage_mean);
FG_CLASS_INT_OUT(strategy_class_fg_get_dc, strategy_fg_get_dc);
FG_CLASS_INT_OUT(strategy_class_fg_get_rm, strategy_fg_get_rm);
FG_CLASS_INT_OUT(strategy_class_fg_get_fcc, strategy_fg_get_fcc);
FG_CLASS_NOARG(strategy_class_fg_is_chip_ok, strategy_fg_is_chip_ok);
FG_CLASS_INT_OUT(strategy_class_fg_get_health, strategy_fg_get_health);
FG_CLASS_INT_OUT(strategy_class_fg_get_first_termination,
		 strategy_fg_get_first_termination);
FG_CLASS_INT_OUT(strategy_class_fg_get_pack_vendor_id,
		 strategy_fg_get_pack_vendor_id);
FG_CLASS_INT_OUT(strategy_class_fg_ops_get_thermal_temperature,
		 strategy_fg_get_thermal_temperature);
FG_CLASS_INT_OUT(strategy_class_fg_get_soh, strategy_fg_get_soh);
FG_CLASS_INT_OUT(strategy_class_fg_get_temp_offset_flag,
		 strategy_fg_get_temp_offset_flag);

int strategy_class_fg_ops_get_soc_decimal(int *decimal, int *rate)
{
	struct strategy_fg_class_ops *ops = READ_ONCE(fg_class_ops);
	if (!decimal || !rate)
		return -EINVAL;
	if (!ops || !ops->strategy_fg_get_soc_decimal_info)
		return -EOPNOTSUPP;
	return ops->strategy_fg_get_soc_decimal_info(READ_ONCE(fg_class_data),
						       decimal, rate);
}
EXPORT_SYMBOL(strategy_class_fg_ops_get_soc_decimal);

bool strategy_class_fg_ops_get_charging_done(void)
{
	struct strategy_fg_class_ops *ops = READ_ONCE(fg_class_ops);
	if (!ops || !ops->strategy_fg_get_charging_done)
		return false;
	return ops->strategy_fg_get_charging_done(READ_ONCE(fg_class_data));
}
EXPORT_SYMBOL(strategy_class_fg_ops_get_charging_done);

int strategy_class_fg_ops_set_charging_done(bool done)
{
	struct strategy_fg_class_ops *ops = READ_ONCE(fg_class_ops);
	if (!ops || !ops->strategy_fg_set_charging_done)
		return -EOPNOTSUPP;
	return ops->strategy_fg_set_charging_done(READ_ONCE(fg_class_data), done);
}
EXPORT_SYMBOL(strategy_class_fg_ops_set_charging_done);

int strategy_class_fg_get_model_name(const char **name)
{
	struct strategy_fg_class_ops *ops = READ_ONCE(fg_class_ops);
	if (!name)
		return -EINVAL;
	if (!ops || !ops->strategy_fg_get_model_name)
		return -EOPNOTSUPP;
	return ops->strategy_fg_get_model_name(READ_ONCE(fg_class_data), name);
}
EXPORT_SYMBOL(strategy_class_fg_get_model_name);

int strategy_class_fg_set_fastcharge(bool en)
{
	struct strategy_fg_class_ops *ops = READ_ONCE(fg_class_ops);
	if (!ops || !ops->strategy_fg_set_fastcharge)
		return -EOPNOTSUPP;
	return ops->strategy_fg_set_fastcharge(READ_ONCE(fg_class_data), en);
}
EXPORT_SYMBOL(strategy_class_fg_set_fastcharge);

int strategy_class_fg_get_fastcharge(void)
{
	struct strategy_fg_class_ops *ops = READ_ONCE(fg_class_ops);
	if (!ops || !ops->strategy_fg_get_fastcharge)
		return -EOPNOTSUPP;
	return ops->strategy_fg_get_fastcharge(READ_ONCE(fg_class_data));
}
EXPORT_SYMBOL(strategy_class_fg_get_fastcharge);

int strategy_class_fg_get_authentic(bool *authentic)
{
	struct strategy_fg_class_ops *ops = READ_ONCE(fg_class_ops);
	if (!authentic)
		return -EINVAL;
	if (!ops || !ops->strategy_fg_get_authentic)
		return -EOPNOTSUPP;
	return ops->strategy_fg_get_authentic(READ_ONCE(fg_class_data), authentic);
}
EXPORT_SYMBOL(strategy_class_fg_get_authentic);

int strategy_class_fg_dual_is_chip_ok(int index)
{
	struct strategy_fg_class_ops *ops = READ_ONCE(fg_class_ops);
	if (!ops || !ops->strategy_fg_dual_is_chip_ok)
		return -EOPNOTSUPP;
	return ops->strategy_fg_dual_is_chip_ok(READ_ONCE(fg_class_data), index);
}
EXPORT_SYMBOL(strategy_class_fg_dual_is_chip_ok);

MODULE_DESCRIPTION("Xiaomi MCA fuel-gauge strategy class");
MODULE_LICENSE("GPL v2");
