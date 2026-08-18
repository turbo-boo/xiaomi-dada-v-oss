// SPDX-License-Identifier: GPL-2.0
/* Xiaomi MCA HL7603 boost-bypass driver for Dada. */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "boost_hl7603"
#endif

#define HL7603_VOUT_REG              0x02
#define HL7603_VOUT_BASE_MV          2850
#define HL7603_VOUT_MAX_MV           5500
#define HL7603_VOUT_STEP_MV          50
#define HL7603_VOUT_DEFAULT_MV       3400

struct hl7603_dev {
	struct device *dev;
	struct i2c_client *client;
	struct notifier_block panel_nb;
	bool support_hbm;
	u32 vout_threshold_mv;
	u32 hbm_vout_threshold_mv;
};

static int hl7603_set_threshold(struct hl7603_dev *info, u32 threshold_mv)
{
	u8 reg;
	int ret;

	if (!info)
		return -EINVAL;
	if (threshold_mv < HL7603_VOUT_BASE_MV ||
	    threshold_mv > HL7603_VOUT_MAX_MV)
		return -ERANGE;

	reg = (threshold_mv - HL7603_VOUT_BASE_MV) /
	      HL7603_VOUT_STEP_MV;
	ret = i2c_smbus_write_byte_data(info->client, HL7603_VOUT_REG, reg);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(info->client, HL7603_VOUT_REG);
	if (ret < 0)
		return ret;
	if ((u8)ret != reg) {
		mca_log_err("threshold verify mismatch wrote=0x%x read=0x%x\n",
			    reg, ret);
		return -EIO;
	}

	mca_log_info("vout threshold=%u mV reg=0x%x\n", threshold_mv, reg);
	return 0;
}

static int hl7603_parse_dt(struct hl7603_dev *info)
{
	struct device_node *np = info->dev->of_node;
	int ret;

	if (!np)
		return -ENODEV;

	ret = mca_parse_dts_u32(np, "vout_threshold",
				&info->vout_threshold_mv,
				HL7603_VOUT_DEFAULT_MV);
	if (ret)
		return ret;

	info->support_hbm = of_property_read_bool(np, "support_hbm");
	if (info->support_hbm) {
		ret = mca_parse_dts_u32(np, "hbm_vout_threshold",
					&info->hbm_vout_threshold_mv,
					info->vout_threshold_mv);
		if (ret)
			return ret;
	}

	return 0;
}

static int hl7603_panel_notifier(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	struct hl7603_dev *info = container_of(nb, struct hl7603_dev, panel_nb);
	int hbm;

	if (event != MCA_EVENT_PANEL_HBM_STATE_CHANGE || !info->support_hbm ||
	    !data)
		return NOTIFY_DONE;

	hbm = *(int *)data;
	(void)hl7603_set_threshold(info, hbm ? info->hbm_vout_threshold_mv :
						 info->vout_threshold_mv);
	return NOTIFY_OK;
}

static int hl7603_probe(struct i2c_client *client)
{
	struct hl7603_dev *info;
	int ret;

	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &client->dev;
	info->client = client;
	i2c_set_clientdata(client, info);

	ret = hl7603_parse_dt(info);
	if (ret)
		return dev_err_probe(&client->dev, ret, "failed to parse HL7603 DT\n");

	ret = hl7603_set_threshold(info, info->vout_threshold_mv);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to initialize HL7603 threshold\n");

	info->panel_nb.notifier_call = hl7603_panel_notifier;
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_PANEL,
					      &info->panel_nb);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to register panel notifier\n");

	mca_log_info("HL7603 boost bypass ready threshold=%u mV\n",
		     info->vout_threshold_mv);
	return 0;
}

static void hl7603_remove(struct i2c_client *client)
{
	struct hl7603_dev *info = i2c_get_clientdata(client);

	if (info)
		mca_event_block_notify_unregister(MCA_EVENT_TYPE_PANEL,
						  &info->panel_nb);
}

static const struct of_device_id hl7603_of_match[] = {
	{ .compatible = "hl7603" },
	{},
};
MODULE_DEVICE_TABLE(of, hl7603_of_match);

static const struct i2c_device_id hl7603_id[] = {
	{ "hl7603", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, hl7603_id);

static struct i2c_driver hl7603_driver = {
	.driver = {
		.name = "hl7603_boost_bypass",
		.of_match_table = hl7603_of_match,
	},
	.probe = hl7603_probe,
	.remove = hl7603_remove,
	.id_table = hl7603_id,
};
module_i2c_driver(hl7603_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA HL7603 boost-bypass driver");
MODULE_LICENSE("GPL v2");
