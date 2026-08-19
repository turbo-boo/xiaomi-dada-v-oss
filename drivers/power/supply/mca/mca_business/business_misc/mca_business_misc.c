// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <mca/common/mca_log.h>
#include "inc/mca_business_misc.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_business_misc"
#endif

static int business_misc_probe(struct platform_device *pdev)
{
	struct business_misc *misc;
	int ret;

	misc = devm_kzalloc(&pdev->dev, sizeof(*misc), GFP_KERNEL);
	if (!misc)
		return -ENOMEM;

	misc->dev = &pdev->dev;
	platform_set_drvdata(pdev, misc);

	ret = business_votable_init(misc->dev);
	if (ret < 0) {
		mca_log_err("business_votable_init failed: %d\n", ret);
		return ret;
	}

	mca_log_info("probe complete\n");
	return 0;
}

static const struct of_device_id business_misc_match_table[] = {
	{ .compatible = "mca,business-misc" },
	{},
};
MODULE_DEVICE_TABLE(of, business_misc_match_table);

static struct platform_driver business_misc_driver = {
	.driver = {
		.name = "business_misc",
		.of_match_table = business_misc_match_table,
	},
	.probe = business_misc_probe,
};
module_platform_driver(business_misc_driver);

MODULE_DESCRIPTION("Xiaomi MCA business misc");
MODULE_LICENSE("GPL v2");
