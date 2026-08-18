// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/spinlock.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_log.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_charge_mievent"
#endif

/*
 * Dada stock exports only the report/state entry points to MCA policy code.
 * Keep those entry points available without inventing the proprietary MIEV
 * transport.  Charging policy must never depend on telemetry delivery.
 */
static DEFINE_SPINLOCK(mca_mievent_lock);
static int mca_mievent_state[MIEVENT_STATE_MAX];

void mca_charge_mievent_set_state(enum charge_mievent_state_ele state, int value)
{
	unsigned long flags;

	if (state < 0 || state >= MIEVENT_STATE_MAX)
		return;
	spin_lock_irqsave(&mca_mievent_lock, flags);
	mca_mievent_state[state] = value;
	spin_unlock_irqrestore(&mca_mievent_lock, flags);
}
EXPORT_SYMBOL(mca_charge_mievent_set_state);

void mca_charge_mievent_report(int event_index, void *data, int size)
{
	int plug_state;
	unsigned long flags;

	if (event_index < 0 || event_index >= CHARGE_DFX_MAX_NUM)
		return;
	spin_lock_irqsave(&mca_mievent_lock, flags);
	plug_state = mca_mievent_state[MIEVENT_STATE_PLUG];
	spin_unlock_irqrestore(&mca_mievent_lock, flags);

	/* Preserve diagnostics locally until Xiaomi's MIEV transport is restored. */
	mca_log_info("mievent idx=%d size=%d plug=%d\n",
		     event_index, size, plug_state);
}
EXPORT_SYMBOL(mca_charge_mievent_report);

MODULE_DESCRIPTION("Xiaomi MCA charge mievent compatibility core");
MODULE_LICENSE("GPL v2");
