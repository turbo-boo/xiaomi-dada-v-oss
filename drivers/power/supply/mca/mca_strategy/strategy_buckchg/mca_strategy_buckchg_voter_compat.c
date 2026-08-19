// SPDX-License-Identifier: GPL-2.0
/*
 * Stock MCA buck-voter ABI bridge for the Dada bring-up strategy.
 *
 * The bring-up strategy predates the stock voter names used by Xiaomi's
 * thermal/policy modules.  Keep its aggregation semantics, but expose the
 * stock names and forward the effective vote into the existing Dada voters.
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_voter.h>
#include <mca/platform/platform_buckchg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "buck_voter_compat"
#endif

#define STOCK_BUCK_VBUS_SPLIT_UV 7000000
#define STOCK_BUCK_DEFAULT_LIMIT INT_MAX

struct stock_buck_vote_bridge {
	struct mca_votable *buck_5v_in;
	struct mca_votable *buck_9v_in;
	struct mca_votable *buck_5v_ich;
	struct mca_votable *buck_9v_ich;
};

static struct stock_buck_vote_bridge g_bridge;
static DEFINE_MUTEX(g_bridge_lock);

static struct mca_votable *dada_input_voter(void)
{
	return mca_find_votable("DADA_BUCK_INPUT_LIMIT");
}

static struct mca_votable *dada_charge_voter(void)
{
	return mca_find_votable("DADA_BUCK_CHARGE_LIMIT");
}

static bool stock_buck_is_9v_path(void)
{
	int vbus = 0;

	if (platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &vbus))
		return false;
	return vbus >= STOCK_BUCK_VBUS_SPLIT_UV;
}

static int stock_buck_forward(struct mca_votable *target, const char *client,
			      bool active, int value)
{
	if (!target)
		return -EAGAIN;
	if (value == STOCK_BUCK_DEFAULT_LIMIT)
		active = false;
	return mca_vote(target, client, active, value);
}

static int stock_buck_5v_in_cb(struct mca_votable *votable, void *data,
			       int result, const char *client)
{
	return stock_buck_forward(dada_input_voter(), "stock_buck_5v_in",
				  !stock_buck_is_9v_path(), result);
}

static int stock_buck_9v_in_cb(struct mca_votable *votable, void *data,
			       int result, const char *client)
{
	return stock_buck_forward(dada_input_voter(), "stock_buck_9v_in",
				  stock_buck_is_9v_path(), result);
}

static int stock_buck_5v_ich_cb(struct mca_votable *votable, void *data,
				int result, const char *client)
{
	return stock_buck_forward(dada_charge_voter(), "stock_buck_5v_ich",
				  !stock_buck_is_9v_path(), result);
}

static int stock_buck_9v_ich_cb(struct mca_votable *votable, void *data,
				int result, const char *client)
{
	return stock_buck_forward(dada_charge_voter(), "stock_buck_9v_ich",
				  stock_buck_is_9v_path(), result);
}

static void stock_buck_destroy_voters(void)
{
	if (!IS_ERR_OR_NULL(g_bridge.buck_9v_ich))
		mca_destroy_votable(g_bridge.buck_9v_ich);
	if (!IS_ERR_OR_NULL(g_bridge.buck_5v_ich))
		mca_destroy_votable(g_bridge.buck_5v_ich);
	if (!IS_ERR_OR_NULL(g_bridge.buck_9v_in))
		mca_destroy_votable(g_bridge.buck_9v_in);
	if (!IS_ERR_OR_NULL(g_bridge.buck_5v_in))
		mca_destroy_votable(g_bridge.buck_5v_in);
	memset(&g_bridge, 0, sizeof(g_bridge));
}

static int __init stock_buck_voter_compat_init(void)
{
	int ret = -ENOMEM;

	mutex_lock(&g_bridge_lock);
	/* Do not create duplicates if a later full stock strategy already owns them. */
	if (mca_find_votable("buck_5v_in") || mca_find_votable("buck_9v_in") ||
	    mca_find_votable("buck_5v_ich") || mca_find_votable("buck_9v_ich")) {
		mutex_unlock(&g_bridge_lock);
		return 0;
	}

	g_bridge.buck_5v_in = mca_create_votable("buck_5v_in", MCA_VOTE_MIN,
						 stock_buck_5v_in_cb,
						 STOCK_BUCK_DEFAULT_LIMIT, NULL);
	if (IS_ERR(g_bridge.buck_5v_in))
		goto out_err;
	g_bridge.buck_9v_in = mca_create_votable("buck_9v_in", MCA_VOTE_MIN,
						 stock_buck_9v_in_cb,
						 STOCK_BUCK_DEFAULT_LIMIT, NULL);
	if (IS_ERR(g_bridge.buck_9v_in))
		goto out_err;
	g_bridge.buck_5v_ich = mca_create_votable("buck_5v_ich", MCA_VOTE_MIN,
						  stock_buck_5v_ich_cb,
						  STOCK_BUCK_DEFAULT_LIMIT, NULL);
	if (IS_ERR(g_bridge.buck_5v_ich))
		goto out_err;
	g_bridge.buck_9v_ich = mca_create_votable("buck_9v_ich", MCA_VOTE_MIN,
						  stock_buck_9v_ich_cb,
						  STOCK_BUCK_DEFAULT_LIMIT, NULL);
	if (IS_ERR(g_bridge.buck_9v_ich))
		goto out_err;

	mca_log_info("stock thermal buck voters registered\n");
	mutex_unlock(&g_bridge_lock);
	return 0;

out_err:
	if (!IS_ERR_OR_NULL(g_bridge.buck_5v_in))
		ret = 0;
	stock_buck_destroy_voters();
	mutex_unlock(&g_bridge_lock);
	return ret ?: -ENOMEM;
}
module_init(stock_buck_voter_compat_init);

static void __exit stock_buck_voter_compat_exit(void)
{
	mutex_lock(&g_bridge_lock);
	stock_buck_destroy_voters();
	mutex_unlock(&g_bridge_lock);
}
module_exit(stock_buck_voter_compat_exit);

MODULE_DESCRIPTION("Xiaomi MCA stock buck voter ABI bridge");
MODULE_LICENSE("GPL v2");
