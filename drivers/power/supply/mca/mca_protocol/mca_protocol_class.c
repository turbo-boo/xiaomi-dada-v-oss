// SPDX-License-Identifier: GPL-2.0
/* Xiaomi MCA adapter protocol class dispatcher for Dada. */
#include <linux/errno.h>
#include <linux/module.h>
#include <mca/protocol/protocol_class.h>

struct mca_protocol_data {
	struct adapter_protocol_class_ops *ops;
	void *data;
};

static struct mca_protocol_data protocol_data[ADAPTER_PROTOCOL_MAX];

static struct mca_protocol_data *mca_protocol_get(unsigned int protocol)
{
	if (protocol >= ADAPTER_PROTOCOL_MAX || !protocol_data[protocol].ops)
		return NULL;
	return &protocol_data[protocol];
}

int protocol_class_register_ops(unsigned int protocol,
				struct adapter_protocol_class_ops *ops, void *data)
{
	if (protocol >= ADAPTER_PROTOCOL_MAX || !ops)
		return -EINVAL;
	protocol_data[protocol].ops = ops;
	protocol_data[protocol].data = data;
	return 0;
}
EXPORT_SYMBOL(protocol_class_register_ops);

int protocol_class_set_adapter_verified(unsigned int protocol, int verified)
{
	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!d || !d->ops->set_adapter_verified)
		return -EOPNOTSUPP;
	return d->ops->set_adapter_verified(d->data, verified);
}
EXPORT_SYMBOL(protocol_class_set_adapter_verified);

int protocol_class_get_adapter_verified(unsigned int protocol, int *verified)
{
	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!verified)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_verified)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_verified(d->data, verified);
}
EXPORT_SYMBOL(protocol_class_get_adapter_verified);

int protocol_class_get_adapter_max_power(unsigned int protocol,
					 unsigned int *max_power)
{
	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!max_power)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_max_power)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_max_power(d->data, max_power);
}
EXPORT_SYMBOL(protocol_class_get_adapter_max_power);

int protocol_class_get_adapter_pwr_max_power(unsigned int protocol,
					     unsigned int *max_power)
{
	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!max_power)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_pwr_max_power)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_pwr_max_power(d->data, max_power);
}
EXPORT_SYMBOL(protocol_class_get_adapter_pwr_max_power);

int protocol_class_det_adapter_type(unsigned int protocol, int en)
{
	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!d || !d->ops->adapter_det_en)
		return -EOPNOTSUPP;
	return d->ops->adapter_det_en(d->data, en);
}
EXPORT_SYMBOL(protocol_class_det_adapter_type);

int protocol_class_get_adapter_type(unsigned int protocol, unsigned int *value)
{
	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!value)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_type)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_type(d->data, (int *)value);
}
EXPORT_SYMBOL(protocol_class_get_adapter_type);

int protocol_class_get_adapter_power_cap(unsigned int protocol,
					 struct adapter_power_cap_info *cap)
{
	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!cap)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_pwr_cap)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_pwr_cap(d->data, cap);
}
EXPORT_SYMBOL(protocol_class_get_adapter_power_cap);

int protocol_class_set_adapter_volt_and_curr(unsigned int protocol,
					     int volt, int curr)
{
	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!d || !d->ops->set_adapter_volt_and_curr)
		return -EOPNOTSUPP;
	return d->ops->set_adapter_volt_and_curr(d->data, volt, curr);
}
EXPORT_SYMBOL(protocol_class_set_adapter_volt_and_curr);

int protocol_class_get_adapter_volt_and_curr(unsigned int protocol,
					     int *volt, int *curr)
{
	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!volt || !curr)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_volt_and_curr)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_volt_and_curr(d->data, volt, curr);
}
EXPORT_SYMBOL(protocol_class_get_adapter_volt_and_curr);

int protocol_class_get_adapter_pps_ptf(unsigned int protocol, int *pps_ptf)
{
	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!pps_ptf)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_pps_ptf)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_pps_ptf(d->data, pps_ptf);
}
EXPORT_SYMBOL(protocol_class_get_adapter_pps_ptf);

int protocol_class_get_adapter_info(unsigned int protocol,
				    struct adapter_vendor_info *info)
{
	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!info)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_info)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_info(d->data, info);
}
EXPORT_SYMBOL(protocol_class_get_adapter_info);

int protocol_class_get_adapter_power_curve(unsigned int protocol,
					   struct adapter_power_curve *curve)
{
	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!curve)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_power_curve)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_power_curve(d->data, curve);
}
EXPORT_SYMBOL(protocol_class_get_adapter_power_curve);

MODULE_DESCRIPTION("Xiaomi MCA adapter protocol class dispatcher");
MODULE_LICENSE("GPL v2");
