// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <mca/common/mca_log.h>
#include <mca/smartchg/smart_chg_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_buckchg_jeita"
#endif

#define MCA_JEITA_BAA_MAX_RECORDS 32

struct dada_jeita_data {
	struct device *dev;
	struct mutex lock;
	struct smart_batt_jeita_term_para *ffc;
	struct smart_batt_jeita_term_para *normal;
	u32 ffc_count;
	u32 normal_count;
	int delta_fv;
	bool baa_ready;
};

static int dada_jeita_copy_records(struct smart_batt_jeita_term_para **dst,
				   u32 *dst_count,
				   const struct smart_batt_jeita_term_para *src,
				   u32 count)
{
	struct smart_batt_jeita_term_para *copy = NULL;
	u32 i;

	if (count > MCA_JEITA_BAA_MAX_RECORDS)
		return -E2BIG;
	if (count) {
		copy = kmemdup(src, sizeof(*src) * count, GFP_KERNEL);
		if (!copy)
			return -ENOMEM;
		for (i = 0; i < count; i++) {
			if (copy[i].t_range.min >= copy[i].t_range.max ||
			    copy[i].t_range.idx < 0) {
				kfree(copy);
				return -EINVAL;
			}
		}
	}

	kfree(*dst);
	*dst = copy;
	*dst_count = count;
	return 0;
}

static int dada_jeita_update_baa_para(void *data, char *baa_para,
				      int ffc_size, int normal_size)
{
	struct dada_jeita_data *info = data;
	const struct smart_batt_jeita_term_para *ffc;
	const struct smart_batt_jeita_term_para *normal;
	int ret;

	if (!info || !baa_para || ffc_size < 0 || normal_size < 0)
		return -EINVAL;
	if (ffc_size > MCA_JEITA_BAA_MAX_RECORDS ||
	    normal_size > MCA_JEITA_BAA_MAX_RECORDS)
		return -E2BIG;

	ffc = (const struct smart_batt_jeita_term_para *)baa_para;
	normal = ffc + ffc_size;

	mutex_lock(&info->lock);
	ret = dada_jeita_copy_records(&info->ffc, &info->ffc_count,
				      ffc, ffc_size);
	if (!ret)
		ret = dada_jeita_copy_records(&info->normal, &info->normal_count,
					      normal, normal_size);
	if (!ret)
		info->baa_ready = true;
	mutex_unlock(&info->lock);

	if (!ret)
		mca_log_info("BAA JEITA updated ffc=%d normal=%d\n",
			     ffc_size, normal_size);
	return ret;
}

static int dada_jeita_set_delta_fv(void *data, int value)
{
	struct dada_jeita_data *info = data;

	if (!info)
		return -EINVAL;
	mutex_lock(&info->lock);
	info->delta_fv = value;
	mutex_unlock(&info->lock);
	return 0;
}

static const struct smart_batt_jeita_term_para *
dada_jeita_find(const struct smart_batt_jeita_term_para *table,
		u32 count, int temp_c)
{
	u32 i;

	for (i = 0; i < count; i++)
		if (temp_c >= table[i].t_range.min &&
		    temp_c < table[i].t_range.max)
			return &table[i];
	return NULL;
}

static int dada_jeita_get_status(int status, void *value, void *data)
{
	struct dada_jeita_data *info = data;
	const struct smart_batt_jeita_term_para *para = NULL;
	int temp = 0, temp_c;
	int ret = 0;

	if (!info || !value)
		return -EINVAL;
	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret)
		return ret;
	temp_c = temp / 10;

	mutex_lock(&info->lock);
	if (!info->baa_ready) {
		ret = -ENODATA;
		goto out;
	}

	switch (status) {
	case STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM:
		para = dada_jeita_find(info->ffc, info->ffc_count, temp_c);
		if (!para) {
			ret = -ERANGE;
			break;
		}
		*(int *)value = para->iterm;
		break;
	case STRATEGY_STATUS_TYPE_JEITA_NORMAL_VTERM:
		para = dada_jeita_find(info->normal, info->normal_count, temp_c);
		if (!para) {
			ret = -ERANGE;
			break;
		}
		*(int *)value = para->vterm;
		break;
	case STRATEGY_STATUS_TYPE_JEITA_FFC_VTERM:
		para = dada_jeita_find(info->ffc, info->ffc_count, temp_c);
		if (!para) {
			ret = -ERANGE;
			break;
		}
		*(int *)value = para->vterm;
		break;
	case STRATEGY_STATUS_TYPE_JEITA_COLD_ZONE:
		*(int *)value = temp_c < 0;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
out:
	mutex_unlock(&info->lock);
	return ret;
}

static int dada_jeita_process_event(int event, int value, void *data)
{
	return 0;
}

static struct mca_smartchg_if_ops dada_jeita_smartchg_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_JEITA,
	.set_delta_fv = dada_jeita_set_delta_fv,
	.update_baa_para = dada_jeita_update_baa_para,
};

static int dada_jeita_probe(struct platform_device *pdev)
{
	struct dada_jeita_data *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);

	ret = mca_strategy_ops_register(STRATEGY_FUNC_TYPE_JEITA,
					dada_jeita_process_event,
					dada_jeita_get_status, NULL, info);
	if (ret)
		return ret;

	dada_jeita_smartchg_ops.data = info;
	ret = mca_smartchg_if_ops_register(&dada_jeita_smartchg_ops);
	if (ret)
		return ret;

	mca_log_info("JEITA BAA consumer registered\n");
	return 0;
}

static int dada_jeita_remove(struct platform_device *pdev)
{
	struct dada_jeita_data *info = platform_get_drvdata(pdev);

	if (!info)
		return 0;
	mutex_lock(&info->lock);
	kfree(info->ffc);
	kfree(info->normal);
	info->ffc = NULL;
	info->normal = NULL;
	info->ffc_count = 0;
	info->normal_count = 0;
	info->baa_ready = false;
	mutex_unlock(&info->lock);
	return 0;
}

static const struct of_device_id dada_jeita_match[] = {
	{ .compatible = "mca,buckchg_jeita" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_jeita_match);

static struct platform_driver dada_jeita_driver = {
	.driver = {
		.name = "mca_buckchg_jeita",
		.of_match_table = dada_jeita_match,
	},
	.probe = dada_jeita_probe,
	.remove = dada_jeita_remove,
};
module_platform_driver(dada_jeita_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA BAA JEITA consumer");
MODULE_LICENSE("GPL v2");
