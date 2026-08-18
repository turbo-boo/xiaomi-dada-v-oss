// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <mca/strategy/strategy_class.h>

struct mca_strategy_func_data {
	mca_strategy_func func;
	mca_strategy_get_status get_func;
	mca_strategy_set_config set_config;
	void *data;
};

static struct mca_strategy_func_data strategy_data[STRATEGY_FUNC_TYPE_MAX];
static DEFINE_MUTEX(strategy_lock);
static bool wls_thermal_remove;

int mca_strategy_func_get_status(int type, int status, void *value)
{
	struct mca_strategy_func_data data;

	if (type < 0 || type >= STRATEGY_FUNC_TYPE_MAX)
		return -EINVAL;
	mutex_lock(&strategy_lock);
	data = strategy_data[type];
	mutex_unlock(&strategy_lock);
	if (!data.get_func)
		return -EOPNOTSUPP;
	return data.get_func(status, value, data.data);
}
EXPORT_SYMBOL(mca_strategy_func_get_status);

int mca_strategy_func_process(unsigned int type, int event, int value)
{
	struct mca_strategy_func_data data;

	if (type >= STRATEGY_FUNC_TYPE_MAX)
		return -EINVAL;
	mutex_lock(&strategy_lock);
	data = strategy_data[type];
	mutex_unlock(&strategy_lock);
	if (!data.func)
		return -EOPNOTSUPP;
	return data.func(event, value, data.data);
}
EXPORT_SYMBOL(mca_strategy_func_process);

int mca_strategy_func_set_config(int type, int config, int value)
{
	struct mca_strategy_func_data data;

	if (type < 0 || type >= STRATEGY_FUNC_TYPE_MAX)
		return -EINVAL;
	mutex_lock(&strategy_lock);
	data = strategy_data[type];
	mutex_unlock(&strategy_lock);
	if (!data.set_config)
		return -EOPNOTSUPP;
	return data.set_config(config, value, data.data);
}
EXPORT_SYMBOL(mca_strategy_func_set_config);

int mca_strategy_ops_register(unsigned int type, mca_strategy_func func,
			      mca_strategy_get_status get_func,
			      mca_strategy_set_config set_config, void *data)
{
	if (type >= STRATEGY_FUNC_TYPE_MAX)
		return -EINVAL;
	mutex_lock(&strategy_lock);
	strategy_data[type].func = func;
	strategy_data[type].get_func = get_func;
	strategy_data[type].set_config = set_config;
	strategy_data[type].data = data;
	mutex_unlock(&strategy_lock);
	return 0;
}
EXPORT_SYMBOL(mca_strategy_ops_register);

int mca_get_wls_charger_thermal_remove(bool *value)
{
	if (!value)
		return -EINVAL;
	*value = READ_ONCE(wls_thermal_remove);
	return 0;
}
EXPORT_SYMBOL(mca_get_wls_charger_thermal_remove);

int mca_set_wls_charger_thermal_remove(bool value)
{
	WRITE_ONCE(wls_thermal_remove, value);
	return 0;
}
EXPORT_SYMBOL(mca_set_wls_charger_thermal_remove);

MODULE_DESCRIPTION("Xiaomi MCA strategy registry");
MODULE_LICENSE("GPL v2");
