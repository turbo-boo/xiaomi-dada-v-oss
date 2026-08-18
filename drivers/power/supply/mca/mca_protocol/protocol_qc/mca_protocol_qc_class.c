// SPDX-License-Identifier: GPL-2.0
/* Xiaomi MCA QC protocol class for Dada. */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_qc_class.h>

struct mca_qc_ops_data {
	struct protocol_class_qc_ops *ops;
	void *data;
};

static struct mca_qc_ops_data qc_data[TYPEC_PORT_MAX];
static unsigned int current_qc_port = TYPEC_PORT_0;

static struct mca_qc_ops_data *mca_qc_get(unsigned int port)
{
	if (port >= TYPEC_PORT_MAX || !qc_data[port].ops)
		return NULL;
	return &qc_data[port];
}

int protocol_class_qc_register_ops(unsigned int port,
				   struct protocol_class_qc_ops *ops, void *data)
{
	if (port >= TYPEC_PORT_MAX || !ops)
		return -EINVAL;
	qc_data[port].ops = ops;
	qc_data[port].data = data;
	return 0;
}
EXPORT_SYMBOL(protocol_class_qc_register_ops);

int protocol_class_qc3_check_class_type(unsigned int port, int *type)
{
	struct mca_qc_ops_data *d = mca_qc_get(port);
	if (!type)
		return -EINVAL;
	if (!d || !d->ops->protocol_qc3_check_class_type)
		return -EOPNOTSUPP;
	return d->ops->protocol_qc3_check_class_type(type, d->data);
}
EXPORT_SYMBOL(protocol_class_qc3_check_class_type);

int protocol_class_qc_get_qc_type(unsigned int port, int *type)
{
	struct mca_qc_ops_data *d = mca_qc_get(port);
	if (!type)
		return -EINVAL;
	if (!d || !d->ops->protocol_qc_get_qc_type)
		return -EOPNOTSUPP;
	return d->ops->protocol_qc_get_qc_type(type, d->data);
}
EXPORT_SYMBOL(protocol_class_qc_get_qc_type);

int protocol_class_qc_set_volt(unsigned int port, int volt)
{
	struct mca_qc_ops_data *d = mca_qc_get(port);
	if (!d || !d->ops->protocol_qc_set_volt)
		return -EOPNOTSUPP;
	return d->ops->protocol_qc_set_volt(d->data, volt);
}
EXPORT_SYMBOL(protocol_class_qc_set_volt);

int protocol_class_qc_set_volt_cmd(unsigned int port, int cmd)
{
	struct mca_qc_ops_data *d = mca_qc_get(port);
	if (!d || !d->ops->protocol_qc_set_volt_cmd)
		return -EOPNOTSUPP;
	return d->ops->protocol_qc_set_volt_cmd(d->data, cmd);
}
EXPORT_SYMBOL(protocol_class_qc_set_volt_cmd);

static int qc_adapter_get_type(void *data, int *type)
{
	return protocol_class_qc_get_qc_type(current_qc_port, type);
}

static int qc_adapter_set_volt_curr(void *data, int volt, int curr)
{
	return protocol_class_qc_set_volt(current_qc_port, volt);
}

static struct adapter_protocol_class_ops qc_adapter_ops = {
	.get_adapter_type = qc_adapter_get_type,
	.set_adapter_volt_and_curr = qc_adapter_set_volt_curr,
};

static int mca_protocol_qc_probe(struct platform_device *pdev)
{
	current_qc_port = TYPEC_PORT_0;
	return protocol_class_register_ops(ADAPTER_PROTOCOL_QC,
					   &qc_adapter_ops, NULL);
}

static const struct of_device_id mca_protocol_qc_of_match[] = {
	{ .compatible = "mca,protocol_qc" },
	{},
};
MODULE_DEVICE_TABLE(of, mca_protocol_qc_of_match);

static struct platform_driver mca_protocol_qc_driver = {
	.driver = {
		.name = "protocol_qc_class",
		.of_match_table = mca_protocol_qc_of_match,
	},
	.probe = mca_protocol_qc_probe,
};
module_platform_driver(mca_protocol_qc_driver);

MODULE_DESCRIPTION("Xiaomi MCA QC protocol class");
MODULE_LICENSE("GPL v2");
