// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pm_wakeup.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_voter.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_business_voter"
#endif

struct business_votable_info {
	struct device *dev;
	struct mca_votable *awake_votable;
	struct mca_votable *smart_batt_votable;
};

static int business_awake_vote_callback(struct mca_votable *votable, void *data,
					int awake, const char *client)
{
	struct business_votable_info *vote_info = data;

	if (awake)
		pm_stay_awake(vote_info->dev);
	else
		pm_relax(vote_info->dev);

	return 0;
}

static int business_smart_batt_vote_callback(struct mca_votable *votable,
					     void *data, int value,
					     const char *client)
{
	mca_log_info("vote SMART_BATT=%d client=%s\n", value,
		     client ? client : "none");
	return 0;
}

int business_votable_init(struct device *dev)
{
	struct business_votable_info *vote_info;
	int ret;

	vote_info = devm_kzalloc(dev, sizeof(*vote_info), GFP_KERNEL);
	if (!vote_info)
		return -ENOMEM;
	vote_info->dev = dev;

	vote_info->awake_votable = mca_create_votable("AWAKE", MCA_VOTE_OR,
					business_awake_vote_callback, 0,
					vote_info);
	if (IS_ERR(vote_info->awake_votable)) {
		ret = PTR_ERR(vote_info->awake_votable);
		vote_info->awake_votable = NULL;
		return ret;
	}

	vote_info->smart_batt_votable = mca_create_votable("SMART_BATT",
						 MCA_VOTE_MIN,
						 business_smart_batt_vote_callback,
						 0, vote_info);
	if (IS_ERR(vote_info->smart_batt_votable)) {
		ret = PTR_ERR(vote_info->smart_batt_votable);
		vote_info->smart_batt_votable = NULL;
		mca_destroy_votable(vote_info->awake_votable);
		vote_info->awake_votable = NULL;
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(business_votable_init);

MODULE_DESCRIPTION("Xiaomi MCA business votables");
MODULE_LICENSE("GPL v2");