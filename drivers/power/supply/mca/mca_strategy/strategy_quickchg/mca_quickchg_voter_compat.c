// SPDX-License-Identifier: GPL-2.0
/*
 * Stock quick-charge voter ABI for Dada.
 *
 * Thermal policy expects these names even before the high-power state machine
 * is allowed to control the charge pump.  Keep the elected limits observable
 * and available to policy code, but deliberately do not start/retune CP here.
 */
#include <linux/module.h>
#include <linux/mutex.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_voter.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "quick_voter_compat"
#endif

#define QUICK_THERMAL_VOTERS 7

struct quick_voter_compat {
	struct mca_votable *voter[QUICK_THERMAL_VOTERS];
	int effective[QUICK_THERMAL_VOTERS];
	struct mutex lock;
};

static struct quick_voter_compat g_qv;
static const char * const quick_voter_names[QUICK_THERMAL_VOTERS] = {
	"div1_single",
	"div1_multi",
	"div2_single",
	"div2_multi",
	"div4_single",
	"div4_multi",
	"thermal_flip",
};

static int quick_voter_index(struct mca_votable *votable)
{
	int i;

	for (i = 0; i < QUICK_THERMAL_VOTERS; i++)
		if (g_qv.voter[i] == votable)
			return i;
	return -EINVAL;
}

static int quick_voter_cb(struct mca_votable *votable, void *data,
			  int result, const char *client)
{
	int idx = quick_voter_index(votable);

	if (idx < 0)
		return idx;
	mutex_lock(&g_qv.lock);
	g_qv.effective[idx] = result;
	mutex_unlock(&g_qv.lock);
	return 0;
}

static void quick_voter_compat_destroy(void)
{
	int i;

	for (i = QUICK_THERMAL_VOTERS - 1; i >= 0; i--) {
		if (!IS_ERR_OR_NULL(g_qv.voter[i]))
			mca_destroy_votable(g_qv.voter[i]);
		g_qv.voter[i] = NULL;
	}
}

static int __init quick_voter_compat_init(void)
{
	int i;

	mutex_init(&g_qv.lock);
	for (i = 0; i < QUICK_THERMAL_VOTERS; i++) {
		if (mca_find_votable(quick_voter_names[i])) {
			mca_log_info("%s already provided by quick-charge strategy\n",
				     quick_voter_names[i]);
			continue;
		}
		g_qv.voter[i] = mca_create_votable(quick_voter_names[i],
						       MCA_VOTE_MIN,
						       quick_voter_cb, 0, NULL);
		if (IS_ERR(g_qv.voter[i])) {
			int ret = PTR_ERR(g_qv.voter[i]);
			g_qv.voter[i] = NULL;
			quick_voter_compat_destroy();
			return ret;
		}
	}

	mca_log_info("stock quick-charge thermal voters registered (CP gated)\n");
	return 0;
}
module_init(quick_voter_compat_init);

static void __exit quick_voter_compat_exit(void)
{
	quick_voter_compat_destroy();
}
module_exit(quick_voter_compat_exit);

MODULE_DESCRIPTION("Xiaomi MCA stock quick-charge voter ABI bridge");
MODULE_LICENSE("GPL v2");
