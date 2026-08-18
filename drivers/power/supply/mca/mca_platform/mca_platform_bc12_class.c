// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/module.h>
#include <mca/platform/platform_bc12_class.h>

struct mca_bc12_data {
	struct platform_bc12_class_ops *ops;
	void *data;
};

static struct mca_bc12_data bc12_data[BC12_MAX_ROLE];

static struct mca_bc12_data *mca_bc12_get(unsigned int role)
{
	if (role >= BC12_MAX_ROLE || !bc12_data[role].ops)
		return NULL;
	return &bc12_data[role];
}

int platform_bc12_class_ops_register(unsigned int role,
				     struct platform_bc12_class_ops *ops,
				     void *data)
{
	if (role >= BC12_MAX_ROLE || !ops)
		return -EINVAL;
	bc12_data[role].ops = ops;
	bc12_data[role].data = data;
	return 0;
}
EXPORT_SYMBOL(platform_bc12_class_ops_register);

int platform_bc12_class_det_enable(unsigned int role, int en)
{
	struct mca_bc12_data *d = mca_bc12_get(role);
	if (!d || !d->ops->bc12_det_en)
		return -EOPNOTSUPP;
	return d->ops->bc12_det_en(en, d->data);
}
EXPORT_SYMBOL(platform_bc12_class_det_enable);

int platform_bc12_class_get_charge_type(unsigned int role, int *type)
{
	struct mca_bc12_data *d = mca_bc12_get(role);
	if (!type)
		return -EINVAL;
	if (!d || !d->ops->get_charge_type)
		return -EOPNOTSUPP;
	return d->ops->get_charge_type(type, d->data);
}
EXPORT_SYMBOL(platform_bc12_class_get_charge_type);

MODULE_DESCRIPTION("Xiaomi MCA BC1.2 class dispatcher");
MODULE_LICENSE("GPL v2");
