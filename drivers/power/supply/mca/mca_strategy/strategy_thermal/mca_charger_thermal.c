// SPDX-License-Identifier: GPL-2.0
/*
 * Dada charger thermal policy.
 *
 * The table layout, sysfs ABI and voter names match Xiaomi's stock MCA
 * charger-thermal module.  Dada uses a 10-column wired table and an optional
 * 10-column wireless table.  High-power CP control remains owned by the
 * quick-charge strategy; this driver only contributes the stock thermal votes.
 */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_voter.h>
#include <mca/protocol/protocol_class.h>
#include <mca/smartchg/smart_chg_class.h>
#include <mca/strategy/strategy_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_thermal"
#endif

#define MCA_THERMAL_MAX_LEVEL 16
#define MCA_THERMAL_WIRED_MODES 10
#define MCA_THERMAL_WLS_MODES 10

/* Same column order as stock mca_charger_thermal.ko. */
enum wired_thermal_mode {
	WIRED_BUCK_5V_IN = 0,
	WIRED_BUCK_9V_IN,
	WIRED_BUCK_5V_ICH,
	WIRED_BUCK_9V_ICH,
	WIRED_DIV1_SINGLE,
	WIRED_DIV1_MULTI,
	WIRED_DIV2_SINGLE,
	WIRED_DIV2_MULTI,
	WIRED_DIV4_SINGLE,
	WIRED_DIV4_MULTI,
};

enum wls_thermal_mode {
	WLS_BPP_IN = 0,
	WLS_BPPQC2_IN,
	WLS_BPPQC3_IN,
	WLS_EPP_IN,
	WLS_AUTH_20W,
	WLS_AUTH_30W,
	WLS_AUTH_50W,
	WLS_AUTH_80W,
	WLS_AUTH_VOICE_BOX,
	WLS_AUTH_MAGNET_30W,
};

enum thermal_sysfs_attr {
	THERMAL_WIRED_CHG_CURR = 0,
	THERMAL_WIRED_CHG_CURR2,
	THERMAL_WIRED_CTRL_LIMIT,
	THERMAL_WIRED_REMOVE,
	THERMAL_WLS_CHG_CURR,
	THERMAL_WLS_CTRL_LIMIT,
	THERMAL_WLS_QUICK_CTRL_LIMIT,
	THERMAL_WLS_REMOVE,
};

struct dada_charger_thermal {
	struct device *dev;
	struct delayed_work voter_work;
	struct thermal_cooling_device *tcd;
	struct mca_votable *wired_voter[MCA_THERMAL_WIRED_MODES];
	struct mca_votable *wls_voter[MCA_THERMAL_WLS_MODES];
	u32 wired[MCA_THERMAL_MAX_LEVEL][MCA_THERMAL_WIRED_MODES];
	u32 wireless[MCA_THERMAL_MAX_LEVEL][MCA_THERMAL_WLS_MODES];
	int wired_max_level;
	int wls_max_level;
	int wired_level;
	int wls_level;
	int wls_quick_level;
	int wired_chg_curr;
	int wls_chg_curr;
	int real_type;
	bool wired_remove;
	bool wls_remove;
	bool wls_super;
	bool support_wireless;
	bool wired_voter_ok;
	bool wls_voter_ok;
};

static struct dada_charger_thermal *g_thermal;

static const char * const wired_voter_names[MCA_THERMAL_WIRED_MODES] = {
	"buck_5v_in", "buck_9v_in", "buck_5v_ich", "buck_9v_ich",
	"div1_single", "div1_multi", "div2_single", "div2_multi",
	"div4_single", "div4_multi",
};

static const char * const wls_voter_names[MCA_THERMAL_WLS_MODES] = {
	"wireless_bpp_in", "wireless_bppqc2_in", "wireless_bppqc3_in",
	"wireless_epp_in", "wireless_auth_20w", "wireless_auth_30w",
	"wireless_auth_50w", "wireless_auth_80w",
	"wireless_auth_voice_box", "wireless_auth_magnet_30w",
};

static int thermal_parse_table(struct device_node *np, const char *name,
			       u32 *table, int columns, int *levels)
{
	int count;

	count = of_property_count_u32_elems(np, name);
	if (count <= 0 || count % columns || count > MCA_THERMAL_MAX_LEVEL * columns)
		return -EINVAL;
	if (of_property_read_u32_array(np, name, table, count))
		return -EINVAL;
	*levels = count / columns;
	return 0;
}

static int thermal_find_voters(struct mca_votable **voters,
			       const char * const *names, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		voters[i] = mca_find_votable(names[i]);
		if (!voters[i])
			return -EAGAIN;
	}
	return 0;
}

static int thermal_refresh_real_type(struct dada_charger_thermal *info)
{
	unsigned int type = XM_CHARGER_TYPE_UNKNOW;

	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PPS, &type) &&
	    type != XM_CHARGER_TYPE_UNKNOW)
		goto out;
	type = XM_CHARGER_TYPE_UNKNOW;
	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD, &type) &&
	    type != XM_CHARGER_TYPE_UNKNOW)
		goto out;
	type = XM_CHARGER_TYPE_UNKNOW;
	(void)protocol_class_get_adapter_type(ADAPTER_PROTOCOL_QC, &type);
out:
	info->real_type = type;
	return type;
}

static bool thermal_cancel_buck_input(struct dada_charger_thermal *info)
{
	int type = info->real_type;

	if (type == XM_CHARGER_TYPE_UNKNOW)
		type = thermal_refresh_real_type(info);
	return type == XM_CHARGER_TYPE_HVDCP3_B ||
	       type == XM_CHARGER_TYPE_HVDCP3P5 ||
	       type == XM_CHARGER_TYPE_PPS ||
	       type == XM_CHARGER_TYPE_PD_VERIFY;
}

static void thermal_clear_votes(struct mca_votable **voters, int count,
				const char *client)
{
	int i;

	for (i = 0; i < count; i++)
		if (voters[i])
			mca_vote(voters[i], client, false, 0);
}

static int thermal_apply_wired(struct dada_charger_thermal *info)
{
	u32 *row = NULL;
	int value, i;
	bool cancel_input;

	if (!info->wired_voter_ok) {
		if (thermal_find_voters(info->wired_voter, wired_voter_names,
					MCA_THERMAL_WIRED_MODES))
			return -EAGAIN;
		info->wired_voter_ok = true;
	}

	if (info->wired_remove) {
		thermal_clear_votes(info->wired_voter, MCA_THERMAL_WIRED_MODES,
				    "mca_thermal");
		return 0;
	}

	if (info->wired_level < 0 || info->wired_level > info->wired_max_level)
		return -ERANGE;
	if (info->wired_level)
		row = info->wired[info->wired_level - 1];

	cancel_input = thermal_cancel_buck_input(info) || !info->wired_level;
	for (i = 0; i < MCA_THERMAL_WIRED_MODES; i++) {
		if (i <= WIRED_BUCK_9V_IN && cancel_input) {
			mca_vote(info->wired_voter[i], "mca_thermal", false, 0);
			continue;
		}

		value = row ? row[i] : 0;
		if (i >= WIRED_BUCK_5V_ICH && info->wired_chg_curr > 0)
			value = value ? min(value, info->wired_chg_curr) :
				info->wired_chg_curr;
		mca_vote(info->wired_voter[i], "mca_thermal", value > 0, value);
		mca_rerun_election(info->wired_voter[i]);
	}
	return 0;
}

static int thermal_apply_wireless(struct dada_charger_thermal *info)
{
	u32 *row = NULL;
	int level, value, i;

	if (!info->support_wireless)
		return -EOPNOTSUPP;
	if (!info->wls_voter_ok) {
		if (thermal_find_voters(info->wls_voter, wls_voter_names,
					MCA_THERMAL_WLS_MODES))
			return -EAGAIN;
		info->wls_voter_ok = true;
	}

	if (info->wls_remove) {
		thermal_clear_votes(info->wls_voter, MCA_THERMAL_WLS_MODES,
				    "mca_wireless_thermal");
		return 0;
	}

	level = info->wls_super ? info->wls_quick_level : info->wls_level;
	if (level < 0 || level > info->wls_max_level)
		return -ERANGE;
	if (level)
		row = info->wireless[level - 1];

	for (i = 0; i < MCA_THERMAL_WLS_MODES; i++) {
		value = row ? row[i] : 0;
		if (i >= WLS_AUTH_20W && info->wls_chg_curr > 0)
			value = value ? min(value, info->wls_chg_curr) :
				info->wls_chg_curr;
		mca_vote(info->wls_voter[i], "mca_wireless_thermal",
			 value > 0, value);
		mca_rerun_election(info->wls_voter[i]);
	}
	return 0;
}

int mca_set_wls_charger_thermal_remove(bool remove)
{
	if (!g_thermal)
		return -ENODEV;
	g_thermal->wls_remove = remove;
	return thermal_apply_wireless(g_thermal) == -EAGAIN ? 0 :
		thermal_apply_wireless(g_thermal);
}
EXPORT_SYMBOL(mca_set_wls_charger_thermal_remove);

int mca_get_wls_charger_thermal_remove(bool *remove)
{
	if (!g_thermal || !remove)
		return -EINVAL;
	*remove = g_thermal->wls_remove;
	return 0;
}
EXPORT_SYMBOL(mca_get_wls_charger_thermal_remove);

static int thermal_process_event(int event, int value, void *data)
{
	struct dada_charger_thermal *info = data;

	if (!info)
		return -EINVAL;
	switch (event) {
	case MCA_EVENT_USB_CONNECT:
		return thermal_apply_wired(info);
	case MCA_EVENT_USB_DISCONNECT:
		thermal_clear_votes(info->wired_voter, MCA_THERMAL_WIRED_MODES,
				    "mca_thermal");
		return 0;
	case MCA_EVENT_CHARGE_TYPE_CHANGE:
		info->real_type = value;
		return thermal_apply_wired(info);
	case MCA_EVENT_WIRELESS_CONNECT:
	case MCA_EVENT_WIRELESS_EPP_MODE:
		return thermal_apply_wireless(info);
	case MCA_EVENT_WIRELESS_DISCONNECT:
		thermal_clear_votes(info->wls_voter, MCA_THERMAL_WLS_MODES,
				    "mca_wireless_thermal");
		return 0;
	default:
		return 0;
	}
}

static int thermal_set_wls_super(void *data, int enable)
{
	struct dada_charger_thermal *info = data;

	if (!info)
		return -EINVAL;
	info->wls_super = !!enable;
	return thermal_apply_wireless(info) == -EAGAIN ? 0 :
		thermal_apply_wireless(info);
}

static struct mca_smartchg_if_ops thermal_smartchg_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_THERMAL,
	.set_wls_super_sts = thermal_set_wls_super,
};

#ifdef CONFIG_SYSFS
static ssize_t thermal_sysfs_show(struct device *dev,
				  struct device_attribute *attr, char *buf);
static ssize_t thermal_sysfs_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count);

static struct mca_sysfs_attr_info thermal_sysfs_fields[] = {
	mca_sysfs_attr_rw(thermal_sysfs, 0664, THERMAL_WIRED_CHG_CURR, wired_chg_curr),
	mca_sysfs_attr_rw(thermal_sysfs, 0664, THERMAL_WIRED_CHG_CURR2, wired_chg_curr2),
	mca_sysfs_attr_rw(thermal_sysfs, 0664, THERMAL_WIRED_CTRL_LIMIT, wired_ctrl_limit),
	mca_sysfs_attr_rw(thermal_sysfs, 0664, THERMAL_WIRED_REMOVE, wired_thermal_remove),
	mca_sysfs_attr_rw(thermal_sysfs, 0664, THERMAL_WLS_CHG_CURR, wireless_chg_curr),
	mca_sysfs_attr_rw(thermal_sysfs, 0664, THERMAL_WLS_CTRL_LIMIT, wireless_ctrl_limit),
	mca_sysfs_attr_rw(thermal_sysfs, 0664, THERMAL_WLS_QUICK_CTRL_LIMIT, wls_quick_chg_control_limit),
	mca_sysfs_attr_rw(thermal_sysfs, 0664, THERMAL_WLS_REMOVE, wireless_thermal_remove),
};
#define THERMAL_SYSFS_COUNT ARRAY_SIZE(thermal_sysfs_fields)
static struct attribute *thermal_sysfs_attrs[THERMAL_SYSFS_COUNT + 1];
static const struct attribute_group thermal_sysfs_group = {
	.attrs = thermal_sysfs_attrs,
};

static struct dada_charger_thermal *thermal_from_attr(struct device *dev,
						      struct device_attribute *attr,
						      struct mca_sysfs_attr_info **field)
{
	*field = mca_sysfs_lookup_attr(attr->attr.name, thermal_sysfs_fields,
				       THERMAL_SYSFS_COUNT);
	return *field ? dev_get_drvdata(dev) : NULL;
}

static ssize_t thermal_sysfs_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *field;
	struct dada_charger_thermal *info = thermal_from_attr(dev, attr, &field);

	if (!info)
		return -ENODEV;
	switch (field->sysfs_attr_name) {
	case THERMAL_WIRED_CHG_CURR:
	case THERMAL_WIRED_CHG_CURR2:
		return sysfs_emit(buf, "%d\n", info->wired_chg_curr * 1000);
	case THERMAL_WIRED_CTRL_LIMIT:
		return sysfs_emit(buf, "%d\n", info->wired_level);
	case THERMAL_WIRED_REMOVE:
		return sysfs_emit(buf, "%d\n", info->wired_remove);
	case THERMAL_WLS_CHG_CURR:
		return sysfs_emit(buf, "%d\n", info->wls_chg_curr * 1000);
	case THERMAL_WLS_CTRL_LIMIT:
		return sysfs_emit(buf, "%d\n", info->wls_level);
	case THERMAL_WLS_QUICK_CTRL_LIMIT:
		return sysfs_emit(buf, "%d\n", info->wls_quick_level);
	case THERMAL_WLS_REMOVE:
		return sysfs_emit(buf, "%d\n", info->wls_remove);
	default:
		return -EINVAL;
	}
}

static ssize_t thermal_sysfs_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *field;
	struct dada_charger_thermal *info = thermal_from_attr(dev, attr, &field);
	int value, ret = 0;

	if (!info || kstrtoint(buf, 0, &value))
		return -EINVAL;
	switch (field->sysfs_attr_name) {
	case THERMAL_WIRED_CHG_CURR:
	case THERMAL_WIRED_CHG_CURR2:
		if (value < 0)
			return -ERANGE;
		info->wired_chg_curr = value / 1000;
		ret = thermal_apply_wired(info);
		break;
	case THERMAL_WIRED_CTRL_LIMIT:
		if (value < 0 || value > info->wired_max_level)
			return -ERANGE;
		info->wired_level = value;
		ret = thermal_apply_wired(info);
		break;
	case THERMAL_WIRED_REMOVE:
		info->wired_remove = !!value;
		ret = thermal_apply_wired(info);
		break;
	case THERMAL_WLS_CHG_CURR:
		if (value < 0)
			return -ERANGE;
		info->wls_chg_curr = value / 1000;
		ret = thermal_apply_wireless(info);
		break;
	case THERMAL_WLS_CTRL_LIMIT:
		if (value < 0 || value > info->wls_max_level)
			return -ERANGE;
		info->wls_level = value;
		ret = thermal_apply_wireless(info);
		break;
	case THERMAL_WLS_QUICK_CTRL_LIMIT:
		if (value < 0 || value > info->wls_max_level)
			return -ERANGE;
		info->wls_quick_level = value;
		ret = thermal_apply_wireless(info);
		break;
	case THERMAL_WLS_REMOVE:
		info->wls_remove = !!value;
		ret = thermal_apply_wireless(info);
		break;
	default:
		return -EINVAL;
	}

	/* Missing wireless strategy voters are not a userspace ABI failure. */
	if (ret && ret != -EAGAIN && ret != -EOPNOTSUPP)
		return ret;
	return count;
}
#endif

static int thermal_get_max_state(struct thermal_cooling_device *tcd,
				 unsigned long *state)
{
	struct dada_charger_thermal *info = tcd->devdata;

	*state = info->wired_max_level;
	return 0;
}

static int thermal_get_cur_state(struct thermal_cooling_device *tcd,
				 unsigned long *state)
{
	struct dada_charger_thermal *info = tcd->devdata;

	*state = info->wired_level;
	return 0;
}

static int thermal_set_cur_state(struct thermal_cooling_device *tcd,
				 unsigned long state)
{
	struct dada_charger_thermal *info = tcd->devdata;

	if (state > info->wired_max_level)
		return -ERANGE;
	info->wired_level = state;
	return thermal_apply_wired(info);
}

static const struct thermal_cooling_device_ops thermal_cdev_ops = {
	.get_max_state = thermal_get_max_state,
	.get_cur_state = thermal_get_cur_state,
	.set_cur_state = thermal_set_cur_state,
};

static void thermal_voter_workfn(struct work_struct *work)
{
	struct dada_charger_thermal *info = container_of(
		work, struct dada_charger_thermal, voter_work.work);

	if (!thermal_find_voters(info->wired_voter, wired_voter_names,
				 MCA_THERMAL_WIRED_MODES)) {
		info->wired_voter_ok = true;
		thermal_apply_wired(info);
		return;
	}
	schedule_delayed_work(&info->voter_work, msecs_to_jiffies(1000));
}

static int thermal_probe(struct platform_device *pdev)
{
	struct dada_charger_thermal *info;
	u32 support = 0;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	info->real_type = XM_CHARGER_TYPE_UNKNOW;
	platform_set_drvdata(pdev, info);

	ret = thermal_parse_table(pdev->dev.of_node, "wired_thermal",
				  &info->wired[0][0], MCA_THERMAL_WIRED_MODES,
				  &info->wired_max_level);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "invalid wired_thermal\n");

	(void)of_property_read_u32(pdev->dev.of_node, "support_wireless", &support);
	info->support_wireless = !!support;
	if (info->support_wireless) {
		ret = thermal_parse_table(pdev->dev.of_node, "wireless_thermal",
					  &info->wireless[0][0],
					  MCA_THERMAL_WLS_MODES,
					  &info->wls_max_level);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "invalid wireless_thermal\n");
	}

#ifdef CONFIG_SYSFS
	mca_sysfs_init_attrs(thermal_sysfs_attrs, thermal_sysfs_fields,
			     THERMAL_SYSFS_COUNT);
	ret = mca_sysfs_create_link_group("charger", "charger_thermal",
					  &pdev->dev, &thermal_sysfs_group);
	if (ret)
		return ret;
#endif

	INIT_DELAYED_WORK(&info->voter_work, thermal_voter_workfn);
	if (thermal_find_voters(info->wired_voter, wired_voter_names,
				MCA_THERMAL_WIRED_MODES))
		schedule_delayed_work(&info->voter_work, msecs_to_jiffies(1000));
	else
		info->wired_voter_ok = true;

	if (info->support_wireless &&
	    !thermal_find_voters(info->wls_voter, wls_voter_names,
				 MCA_THERMAL_WLS_MODES))
		info->wls_voter_ok = true;

	thermal_smartchg_ops.data = info;
	(void)mca_smartchg_if_ops_register(&thermal_smartchg_ops);
	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_THERMAL,
					thermal_process_event, NULL, NULL, info);

	info->tcd = devm_thermal_of_cooling_device_register(
		&pdev->dev, pdev->dev.of_node, "battery", info, &thermal_cdev_ops);
	if (IS_ERR(info->tcd))
		mca_log_err("thermal cooling device register failed: %ld\n",
			    PTR_ERR(info->tcd));

	g_thermal = info;
	mca_log_info("ready wired_levels=%d wireless_levels=%d\n",
		     info->wired_max_level, info->wls_max_level);
	return 0;
}

static int thermal_remove(struct platform_device *pdev)
{
	struct dada_charger_thermal *info = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&info->voter_work);
	thermal_clear_votes(info->wired_voter, MCA_THERMAL_WIRED_MODES,
			    "mca_thermal");
	thermal_clear_votes(info->wls_voter, MCA_THERMAL_WLS_MODES,
			    "mca_wireless_thermal");
#ifdef CONFIG_SYSFS
	mca_sysfs_remove_link_group("charger", "charger_thermal", &pdev->dev,
				    &thermal_sysfs_group);
#endif
	if (g_thermal == info)
		g_thermal = NULL;
	return 0;
}

static const struct of_device_id thermal_match[] = {
	{ .compatible = "mca_charger_thermal" },
	{},
};
MODULE_DEVICE_TABLE(of, thermal_match);

static struct platform_driver thermal_driver = {
	.driver = {
		.name = "mca_charger_thermal",
		.of_match_table = thermal_match,
	},
	.probe = thermal_probe,
	.remove = thermal_remove,
};
module_platform_driver(thermal_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA charger thermal policy");
MODULE_LICENSE("GPL v2");
