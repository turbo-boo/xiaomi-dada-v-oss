#ifndef _MCA_COMMON_MCA_WORKQUEUE_H_
#define _MCA_COMMON_MCA_WORKQUEUE_H_

#include <linux/workqueue.h>

int mca_queue_delayed_work(struct delayed_work *dwork, unsigned long delay);
int mca_mod_delayed_work(struct delayed_work *dwork, unsigned long delay);
int mca_queue_work(struct work_struct *work);
int mca_cancel_work(struct work_struct *work);
int mca_cancel_delayed_work(struct delayed_work *dwork);

static inline void mca_cancel_delayed_work_sync(struct delayed_work *dwork)
{
	/* Force-stop voters can run from the monitor they are stopping. */
	if (current_work() == &dwork->work)
		cancel_delayed_work(dwork);
	else
		cancel_delayed_work_sync(dwork);
}

#endif /* _MCA_COMMON_MCA_WORKQUEUE_H_ */
