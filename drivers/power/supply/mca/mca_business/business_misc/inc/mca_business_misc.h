/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BUSINESS_MISC_H__
#define __BUSINESS_MISC_H__

struct device;

struct business_misc {
	struct device *dev;
};

int business_votable_init(struct device *dev);

#endif /* __BUSINESS_MISC_H__ */
