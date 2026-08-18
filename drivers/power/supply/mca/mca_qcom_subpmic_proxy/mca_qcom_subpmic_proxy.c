// SPDX-License-Identifier: GPL-2.0
/*
 * Dada MCA Qualcomm sub-PMIC bring-up proxy.
 *
 * The stock Xiaomi proxy speaks a large private property ABI over MCA
 * PMIC-GLINK.  Establish the wired-charging MCA contract first by bridging
 * the stable Linux power_supply view exported by Qualcomm's charger stack.
 * Private QC/LPD/AICL controls are added on the direct GLINK path separately.
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/platform/platform_bc12_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/protocol/protocol_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "qcom_subpmic"
#endif

struct dada_subpmic {
	struct device *dev;
	struct power_supply *usb;
	struct power_supply *battery;
	struct notifier_block psy_nb;
	bool usb_online;
};

static struct power_supply *dada_subpmic_get_psy(struct dada_subpmic *sp,
						 const char *name)
{
	struct power_supply **slot;

	if (!strcmp(name, "usb"))
		slot = &sp->usb;
	else
		slot = &sp->battery;

	if (!*slot)
		*slot = power_supply_get_by_name(name);
	return *slot;
}

static int dada_psy_get(struct dada_subpmic *sp, const char *name,
			 enum power_supply_property prop, int *value)
{
	struct power_supply *psy;
	union power_supply_propval pval = { 0 };
	int ret;

	if (!value)
		return -EINVAL;
	psy = dada_subpmic_get_psy(sp, name);
	if (!psy)
		return -EPROBE_DEFER;
	ret = power_supply_get_property(psy, prop, &pval);
	if (!ret)
		*value = pval.intval;
	return ret;
}

static int dada_psy_set(struct dada_subpmic *sp, const char *name,
			 enum power_supply_property prop, int value)
{
	struct power_supply *psy;
	union power_supply_propval pval = { .intval = value };

	psy = dada_subpmic_get_psy(sp, name);
	if (!psy)
		return -EPROBE_DEFER;
	return power_supply_set_property(psy, prop, &pval);
}

static int dada_buck_get_online(void *data, int *online)
{
	return dada_psy_get(data, "usb", POWER_SUPPLY_PROP_ONLINE, online);
}

static int dada_buck_get_bus_volt(void *data, int *mv)
{
	int uv = 0;
	int ret = dada_psy_get(data, "usb", POWER_SUPPLY_PROP_VOLTAGE_NOW, &uv);

	if (!ret)
		*mv = DIV_ROUND_CLOSEST(uv, 1000);
	return ret;
}

static int dada_buck_get_bus_curr(void *data, int *ma)
{
	int ua = 0;
	int ret = dada_psy_get(data, "usb", POWER_SUPPLY_PROP_CURRENT_NOW, &ua);

	if (!ret)
		*ma = DIV_ROUND_CLOSEST(ua, 1000);
	return ret;
}

static int dada_buck_get_batt_volt(void *data, int *mv)
{
	int uv = 0;
	int ret = dada_psy_get(data, "battery", POWER_SUPPLY_PROP_VOLTAGE_NOW,
			       &uv);

	if (!ret)
		*mv = DIV_ROUND_CLOSEST(uv, 1000);
	return ret;
}

static int dada_buck_get_batt_curr(void *data, int *ma)
{
	int ua = 0;
	int ret = dada_psy_get(data, "battery", POWER_SUPPLY_PROP_CURRENT_NOW,
			       &ua);

	if (!ret)
		*ma = DIV_ROUND_CLOSEST(ua, 1000);
	return ret;
}

static int dada_buck_get_batt_tsns(void *data, int *temp)
{
	return dada_psy_get(data, "battery", POWER_SUPPLY_PROP_TEMP, temp);
}

static int dada_buck_get_chg_status(void *data, int *status)
{
	return dada_psy_get(data, "battery", POWER_SUPPLY_PROP_STATUS, status);
}

static int dada_buck_is_charge_done(void *data, bool *done)
{
	int status = POWER_SUPPLY_STATUS_UNKNOWN;
	int ret;

	if (!done)
		return -EINVAL;
	ret = dada_buck_get_chg_status(data, &status);
	if (!ret)
		*done = status == POWER_SUPPLY_STATUS_FULL;
	return ret;
}

static int dada_buck_get_input_curr_lmt(void *data, int *ma)
{
	int ua = 0;
	int ret = dada_psy_get(data, "usb", POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
			       &ua);

	if (!ret)
		*ma = DIV_ROUND_CLOSEST(ua, 1000);
	return ret;
}

static int dada_buck_set_input_curr_lmt(void *data, int ma)
{
	return dada_psy_set(data, "usb", POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
			    ma * 1000);
}

static int dada_buck_set_ichg(void *data, int ma)
{
	return dada_psy_set(data, "battery",
			    POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, ma * 1000);
}

static int dada_buck_set_term_curr(void *data, int ma)
{
	return dada_psy_set(data, "battery", POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
			    ma * 1000);
}

static int dada_buck_set_term_volt(void *data, int mv)
{
	return dada_psy_set(data, "battery",
			    POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, mv * 1000);
}

static int dada_buck_get_term_curr(void *data, int *ma)
{
	int ua = 0;
	int ret = dada_psy_get(data, "battery", POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
			       &ua);

	if (!ret)
		*ma = DIV_ROUND_CLOSEST(ua, 1000);
	return ret;
}

static int dada_buck_get_term_volt(void *data, int *mv)
{
	int uv = 0;
	int ret = dada_psy_get(data, "battery",
			       POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &uv);

	if (!ret)
		*mv = DIV_ROUND_CLOSEST(uv, 1000);
	return ret;
}

static int dada_buck_is_init_ok(void *data)
{
	struct dada_subpmic *sp = data;
	int online;

	if (!dada_subpmic_get_psy(sp, "usb"))
		return 0;
	/* The charger can legitimately be offline; readable ONLINE proves init. */
	return !dada_buck_get_online(sp, &online);
}

static int dada_bc12_get_type(int *type, void *data)
{
	struct dada_subpmic *sp = data;
	int usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	int ret;

	if (!type)
		return -EINVAL;
	ret = dada_psy_get(sp, "usb", POWER_SUPPLY_PROP_USB_TYPE, &usb_type);
	if (ret)
		return ret;

	switch (usb_type) {
	case POWER_SUPPLY_USB_TYPE_SDP:
		*type = XM_CHARGER_TYPE_SDP;
		break;
	case POWER_SUPPLY_USB_TYPE_CDP:
		*type = XM_CHARGER_TYPE_CDP;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
		*type = XM_CHARGER_TYPE_DCP;
		break;
	default:
		*type = XM_CHARGER_TYPE_UNKNOW;
		break;
	}
	return 0;
}

static int dada_bc12_det_en(int en, void *data)
{
	/* Qualcomm's GLINK charger owns BC1.2 detection in this bring-up path. */
	return 0;
}

static int dada_subpmic_psy_notifier(struct notifier_block *nb,
				      unsigned long event, void *ptr)
{
	struct dada_subpmic *sp = container_of(nb, struct dada_subpmic, psy_nb);
	struct power_supply *psy = ptr;
	int online = 0;
	int type = XM_CHARGER_TYPE_UNKNOW;

	if (event != PSY_EVENT_PROP_CHANGED || !psy || !psy->desc ||
	    strcmp(psy->desc->name, "usb"))
		return NOTIFY_DONE;

	if (!dada_buck_get_online(sp, &online) && online != sp->usb_online) {
		sp->usb_online = online;
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT,
			online ? MCA_EVENT_USB_CONNECT : MCA_EVENT_USB_DISCONNECT,
			NULL);
	}
	if (!dada_bc12_get_type(&type, sp))
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_TYPE,
				       MCA_EVENT_CHARGE_TYPE_CHANGE, &type);
	return NOTIFY_OK;
}

static struct platform_class_buckchg_ops dada_buck_ops = {
	.get_online = dada_buck_get_online,
	.is_charge_done = dada_buck_is_charge_done,
	.get_input_curr_lmt = dada_buck_get_input_curr_lmt,
	.get_bus_curr = dada_buck_get_bus_curr,
	.get_bus_volt = dada_buck_get_bus_volt,
	.get_batt_volt = dada_buck_get_batt_volt,
	.get_batt_curr = dada_buck_get_batt_curr,
	.get_batt_tsns = dada_buck_get_batt_tsns,
	.get_chg_status = dada_buck_get_chg_status,
	.get_term_curr = dada_buck_get_term_curr,
	.get_term_volt = dada_buck_get_term_volt,
	.set_input_curr_lmt = dada_buck_set_input_curr_lmt,
	.set_ichg = dada_buck_set_ichg,
	.set_term_curr = dada_buck_set_term_curr,
	.set_term_volt = dada_buck_set_term_volt,
	.is_init_ok = dada_buck_is_init_ok,
};

static struct platform_bc12_class_ops dada_bc12_ops = {
	.bc12_det_en = dada_bc12_det_en,
	.get_charge_type = dada_bc12_get_type,
};

static int dada_subpmic_probe(struct platform_device *pdev)
{
	struct dada_subpmic *sp;
	int ret;

	sp = devm_kzalloc(&pdev->dev, sizeof(*sp), GFP_KERNEL);
	if (!sp)
		return -ENOMEM;
	sp->dev = &pdev->dev;
	platform_set_drvdata(pdev, sp);

	ret = platform_class_buckchg_ops_register(MAIN_BUCK_CHARGER, sp,
						  &dada_buck_ops);
	if (ret)
		return ret;
	ret = platform_bc12_class_ops_register(BC12_MAIN_ROLE,
					       &dada_bc12_ops, sp);
	if (ret)
		return ret;

	sp->psy_nb.notifier_call = dada_subpmic_psy_notifier;
	ret = power_supply_reg_notifier(&sp->psy_nb);
	if (ret)
		mca_log_info("power-supply notifier unavailable: %d\n", ret);

	mca_log_info("wired sub-PMIC bring-up proxy registered\n");
	return 0;
}

static int dada_subpmic_remove(struct platform_device *pdev)
{
	struct dada_subpmic *sp = platform_get_drvdata(pdev);

	if (!sp)
		return 0;
	power_supply_unreg_notifier(&sp->psy_nb);
	if (sp->usb)
		power_supply_put(sp->usb);
	if (sp->battery)
		power_supply_put(sp->battery);
	return 0;
}

static const struct of_device_id dada_subpmic_match[] = {
	{ .compatible = "mca,qcom_subpmic" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_subpmic_match);

static struct platform_driver dada_subpmic_driver = {
	.driver = {
		.name = "mca_qcom_subpmic_proxy",
		.of_match_table = dada_subpmic_match,
	},
	.probe = dada_subpmic_probe,
	.remove = dada_subpmic_remove,
};
module_platform_driver(dada_subpmic_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA Qualcomm sub-PMIC bring-up proxy");
MODULE_LICENSE("GPL v2");
