// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/module.h>

#include <mca/platform/platform_wireless_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_wireless_class.h>

int strategy_class_wireless_ops_get_adapter_power(
	struct wls_adapter_power_cap *adapter_power)
{
	int max_fcc = 0;
	int max_power = 0;

	if (!adapter_power)
		return -EINVAL;
	if (mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
					 STRATEGY_STATUS_TYPE_POWER_MAX,
					 &max_power))
		return -ENODATA;

	/*
	 * The quick-wireless layer owns the effective battery-current ceiling.
	 * Keep this compatibility helper read-only: failure to obtain the quick
	 * strategy status must not block basic wireless power reporting.
	 */
	if (mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
					 STRATEGY_STATUS_TYPE_QC_IBAT_MAX,
					 &max_fcc))
		max_fcc = 0;

	adapter_power->max_fcc = max_fcc;
	adapter_power->max_power = max_power;
	return 0;
}
EXPORT_SYMBOL(strategy_class_wireless_ops_get_adapter_power);

int strategy_class_wireless_ops_set_parallel_charge(bool parallel_charge_flag)
{
	return platform_class_wireless_set_parallel_charge(WIRELESS_ROLE_MASTER,
							    parallel_charge_flag);
}
EXPORT_SYMBOL(strategy_class_wireless_ops_set_parallel_charge);

int strategy_class_wireless_ops_get_wls_type(int *wls_type)
{
	u8 mode = 0;
	int ret;

	if (!wls_type)
		return -EINVAL;
	ret = platform_class_wireless_get_rx_power_mode(WIRELESS_ROLE_MASTER,
							&mode);
	if (ret) {
		*wls_type = XM_WLS_CHARGER_TYPE_UNKNOWN;
		return ret;
	}
	*wls_type = mode ? XM_WLS_CHARGER_TYPE_EPP : XM_WLS_CHARGER_TYPE_BPP;
	return 0;
}
EXPORT_SYMBOL(strategy_class_wireless_ops_get_wls_type);

int strategy_class_wireless_ops_get_adapter_charger_mode(int *cp_charger_mode)
{
	if (!cp_charger_mode)
		return -EINVAL;
	/* SC8585 ratio selection is intentionally deferred until hardware parity. */
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(strategy_class_wireless_ops_get_adapter_charger_mode);

void strategy_class_wireless_op_get_rx_iout_limit(int *rx_iout_limit_ma)
{
	if (rx_iout_limit_ma)
		*rx_iout_limit_ma = 0;
}
EXPORT_SYMBOL(strategy_class_wireless_op_get_rx_iout_limit);

void strategy_wireless_enable_cp_error_irq(unsigned int enable)
{
	/* Quick-wireless CP IRQ policy is not enabled before SC8585 validation. */
}
EXPORT_SYMBOL(strategy_wireless_enable_cp_error_irq);

MODULE_DESCRIPTION("Xiaomi Dada MCA wireless strategy compatibility ABI");
MODULE_LICENSE("GPL v2");
