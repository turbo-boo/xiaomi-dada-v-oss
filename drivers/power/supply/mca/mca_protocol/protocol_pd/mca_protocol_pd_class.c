// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi MCA PD/PPS protocol bridge for Dada.
 *
 * Transport drivers (ADSP PMIC-GLINK on Dada) register protocol_class_pd_ops
 * for a Type-C port.  This file exposes the public PD ABI and bridges it to
 * the generic ADAPTER_PROTOCOL_PD/PPS class used by MCA business/strategy.
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>

struct mca_pd_ops_data {
	struct protocol_class_pd_ops *ops;
	void *data;
};

static struct mca_pd_ops_data pd_data[TYPEC_PORT_MAX];
static unsigned int current_port = TYPEC_PORT_0;

static struct mca_pd_ops_data *mca_pd_get(unsigned int port)
{
	if (port >= TYPEC_PORT_MAX || !pd_data[port].ops)
		return NULL;
	return &pd_data[port];
}

int protocol_class_pd_register_ops(unsigned int port,
				   struct protocol_class_pd_ops *ops, void *data)
{
	if (port >= TYPEC_PORT_MAX || !ops)
		return -EINVAL;
	pd_data[port].ops = ops;
	pd_data[port].data = data;
	return 0;
}
EXPORT_SYMBOL(protocol_class_pd_register_ops);

#define PD_INT_IN(_fn, _op) \
int _fn(unsigned int port, int val) \
{ \
	struct mca_pd_ops_data *d = mca_pd_get(port); \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(val, d->data); \
} \
EXPORT_SYMBOL(_fn)

#define PD_INT_OUT(_fn, _op) \
int _fn(unsigned int port, int *val) \
{ \
	struct mca_pd_ops_data *d = mca_pd_get(port); \
	if (!val) \
		return -EINVAL; \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(val, d->data); \
} \
EXPORT_SYMBOL(_fn)

#define PD_U32_IN(_fn, _op) \
int _fn(unsigned int port, unsigned int val) \
{ \
	struct mca_pd_ops_data *d = mca_pd_get(port); \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(val, d->data); \
} \
EXPORT_SYMBOL(_fn)

#define PD_U32_OUT(_fn, _op) \
int _fn(unsigned int port, unsigned int *val) \
{ \
	struct mca_pd_ops_data *d = mca_pd_get(port); \
	if (!val) \
		return -EINVAL; \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(val, d->data); \
} \
EXPORT_SYMBOL(_fn)

#define PD_BOOL_IN(_fn, _op) \
int _fn(unsigned int port, bool val) \
{ \
	struct mca_pd_ops_data *d = mca_pd_get(port); \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(val, d->data); \
} \
EXPORT_SYMBOL(_fn)

#define PD_BOOL_OUT(_fn, _op) \
int _fn(unsigned int port, bool *val) \
{ \
	struct mca_pd_ops_data *d = mca_pd_get(port); \
	if (!val) \
		return -EINVAL; \
	if (!d || !d->ops->_op) \
		return -EOPNOTSUPP; \
	return d->ops->_op(val, d->data); \
} \
EXPORT_SYMBOL(_fn)

PD_U32_OUT(protocol_class_pd_get_pps_max_power, protocol_pd_pps_get_max_power);
PD_INT_OUT(protocol_class_pd_get_pps_ptf, protocol_pd_get_pps_ptf);
PD_INT_IN(protocol_class_pd_set_fixed_volt, protocol_pd_fixed_pdo_set_vol);
PD_INT_IN(protocol_class_pd_set_gear_shift, protocol_pd_set_gear_shift);
PD_U32_IN(protocol_class_pd_set_pps_max_cur, protocol_pd_set_pps_max_cur);
PD_U32_OUT(protocol_class_pd_get_pps_max_cur, protocol_pd_get_pps_max_cur);
PD_INT_IN(protocol_class_pd_set_pd_active, protocol_pd_set_pd_active);
PD_INT_OUT(protocol_class_pd_get_pd_active, protocol_pd_get_pd_active);
PD_U32_IN(protocol_class_pd_set_pps_min_volt, protocol_pd_set_pps_min_volt);
PD_U32_OUT(protocol_class_pd_get_pps_min_volt, protocol_pd_get_pps_min_volt);
PD_U32_IN(protocol_class_pd_set_pps_max_volt, protocol_pd_set_pps_max_volt);
PD_U32_OUT(protocol_class_pd_get_pps_max_volt, protocol_pd_get_pps_max_volt);
PD_U32_IN(protocol_class_pd_set_pps_apdo_max, protocol_pd_set_pps_apdo_max);
PD_U32_OUT(protocol_class_pd_get_pps_apdo_max, protocol_pd_get_pps_apdo_max);
PD_INT_OUT(protocol_class_pd_get_pd_type, protocol_pd_get_pd_type);
PD_INT_IN(protocol_class_pd_set_typec_mode, protocol_pd_set_typec_mode);
PD_INT_OUT(protocol_class_pd_get_typec_mode, protocol_pd_get_typec_mode);
PD_INT_OUT(protocol_class_pd_get_typec_cc_orientation,
	   protocol_pd_get_typec_cc_orientation);
PD_INT_IN(protocol_class_pd_set_typec_cc_orientation,
	  protocol_pd_set_typec_cc_orientation);
PD_INT_IN(protocol_class_pd_set_pd_in_hard_reset,
	  protocol_pd_set_in_hard_reset);
PD_INT_OUT(protocol_class_pd_get_pd_in_hard_reset,
	   protocol_pd_get_in_hard_reset);
PD_INT_IN(protocol_class_pd_set_usb_suspend_supported,
	  protocol_pd_set_usb_suspend_supported);
PD_INT_OUT(protocol_class_pd_get_usb_suspend_supported,
	   protocol_pd_get_usb_suspend_supported);
PD_INT_IN(protocol_class_pd_set_pd_typec_accessory_mode,
	  protocol_pd_set_pd_typec_accessory_mode);
PD_INT_OUT(protocol_class_pd_get_pd_typec_accessory_mode,
	   protocol_pd_get_pd_typec_accessory_mode);
PD_INT_IN(protocol_class_pd_set_verify_process, protocol_pd_set_verify_process);
PD_INT_OUT(protocol_class_pd_get_verify_process, protocol_pd_get_verify_process);
PD_INT_IN(protocol_class_pd_set_pd_verifed, protocol_pd_set_pd_verifed);
PD_INT_OUT(protocol_class_pd_get_pd_verifed, protocol_pd_get_pd_verifed);
PD_BOOL_OUT(protocol_class_pd_get_has_dp, protocol_pd_get_has_dp);
PD_BOOL_OUT(protocol_class_pd_get_cid_status, protocol_pd_get_cid_status);
PD_BOOL_OUT(protocol_class_pd_get_otg_plugin_status,
	    protocol_pd_get_otg_plugin_status);
PD_BOOL_IN(protocol_class_pd_set_cc_toggle, protocol_pd_set_cc_toggle);
PD_BOOL_OUT(protocol_class_pd_get_cc_toggle, protocol_pd_get_cc_toggle);
PD_INT_OUT(protocol_class_pd_get_snk_src_mode, protocol_pd_get_snk_src_mode);
PD_BOOL_OUT(protocol_class_pd_get_cc_status, protocol_pd_get_cc_status);
PD_INT_OUT(protocol_class_pd_get_cc_short_vbus, protocol_pd_get_cc_short_vbus);
PD_BOOL_OUT(protocol_class_pd_get_suspend_support_status,
	    protocol_pd_get_suspend_support_status);
PD_INT_OUT(protocol_class_pd_get_zimi_cypress_flag,
	   protocol_pd_get_zimi_cypress_flag);

int protocol_class_pd_set_pps_pdo_select(unsigned int port, int volt, int curr)
{
	struct mca_pd_ops_data *d = mca_pd_get(port);
	if (!d || !d->ops->protocol_pd_pps_pdo_select)
		return -EOPNOTSUPP;
	return d->ops->protocol_pd_pps_pdo_select(volt, curr, d->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_pps_pdo_select);

int protocol_class_pd_get_pps_status(unsigned int port, int *volt, int *curr)
{
	struct mca_pd_ops_data *d = mca_pd_get(port);
	if (!volt || !curr)
		return -EINVAL;
	if (!d || !d->ops->protocol_pd_get_pps_status)
		return -EOPNOTSUPP;
	return d->ops->protocol_pd_get_pps_status(volt, curr, d->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pps_status);

int protocol_class_pd_get_cap(unsigned int port, int cap_type,
			      struct adapter_power_cap *cap)
{
	struct mca_pd_ops_data *d = mca_pd_get(port);
	if (!cap)
		return -EINVAL;
	if (!d || !d->ops->protocol_pd_get_cap)
		return -EOPNOTSUPP;
	return d->ops->protocol_pd_get_cap(cap_type, cap, d->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_cap);

int protocol_class_pd_get_adapter_id(unsigned int port, unsigned int *id)
{
	struct mca_pd_ops_data *d = mca_pd_get(port);
	if (!id)
		return -EINVAL;
	if (!d || !d->ops->protocol_pd_get_adapter_id)
		return -EOPNOTSUPP;
	return d->ops->protocol_pd_get_adapter_id(id, d->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_adapter_id);

int protocol_class_pd_get_adapter_svid(unsigned int port, unsigned int *svid)
{
	struct mca_pd_ops_data *d = mca_pd_get(port);
	if (!svid)
		return -EINVAL;
	if (!d || !d->ops->protocol_pd_get_adapter_svid)
		return -EOPNOTSUPP;
	return d->ops->protocol_pd_get_adapter_svid(svid, d->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_adapter_svid);

int protocol_class_pd_request_vdm_cmd(unsigned int port, enum uvdm_state cmd,
				      unsigned int *data,
				      unsigned int data_len)
{
	struct mca_pd_ops_data *d = mca_pd_get(port);
	if (!data)
		return -EINVAL;
	if (!d || !d->ops->protocol_pd_request_vdm_cmd)
		return -EOPNOTSUPP;
	return d->ops->protocol_pd_request_vdm_cmd(cmd, data, data_len, d->data);
}
EXPORT_SYMBOL(protocol_class_pd_request_vdm_cmd);

int protocol_class_pd_get_vdm_cmd(unsigned int port, int *cmd,
				  struct usbpd_vdm_data *vdm)
{
	struct mca_pd_ops_data *d = mca_pd_get(port);
	if (!cmd || !vdm)
		return -EINVAL;
	if (!d || !d->ops->protocol_pd_get_vdm_cmd)
		return -EOPNOTSUPP;
	return d->ops->protocol_pd_get_vdm_cmd(cmd, vdm, d->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_vdm_cmd);

int protocol_class_pd_get_power_role(unsigned int port, unsigned char *role)
{
	struct mca_pd_ops_data *d = mca_pd_get(port);
	if (!role)
		return -EINVAL;
	if (!d || !d->ops->protocol_pd_get_power_role)
		return -EOPNOTSUPP;
	return d->ops->protocol_pd_get_power_role(role, d->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_power_role);

int protocol_class_pd_get_data_role(unsigned int port, unsigned char *role)
{
	struct mca_pd_ops_data *d = mca_pd_get(port);
	if (!role)
		return -EINVAL;
	if (!d || !d->ops->protocol_pd_get_data_role)
		return -EOPNOTSUPP;
	return d->ops->protocol_pd_get_data_role(role, d->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_data_role);

int protocol_class_pd_get_current_state(unsigned int port, char *state, int len)
{
	struct mca_pd_ops_data *d = mca_pd_get(port);
	if (!state || len <= 0)
		return -EINVAL;
	if (!d || !d->ops->protocol_pd_get_current_state)
		return -EOPNOTSUPP;
	return d->ops->protocol_pd_get_current_state(state, len, d->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_current_state);

int protocol_class_pd_get_pdos(unsigned int port, struct pd_pdo *pdos, int count)
{
	struct mca_pd_ops_data *d = mca_pd_get(port);
	if (!pdos || count <= 0)
		return -EINVAL;
	if (!d || !d->ops->protocol_pd_get_pdos)
		return -EOPNOTSUPP;
	return d->ops->protocol_pd_get_pdos(pdos, count, d->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pdos);

int protocol_class_pd_get_port_num(void)
{
	return current_port;
}
EXPORT_SYMBOL(protocol_class_pd_get_port_num);

static int pd_adapter_set_verified(void *data, int verified)
{
	return protocol_class_pd_set_pd_verifed(current_port, verified);
}

static int pd_adapter_get_verified(void *data, int *verified)
{
	return protocol_class_pd_get_pd_verifed(current_port, verified);
}

static int pd_adapter_get_type(void *data, int *type)
{
	return protocol_class_pd_get_pd_type(current_port, type);
}

static int pd_adapter_get_fixed_caps(void *data,
				     struct adapter_power_cap_info *caps)
{
	struct pd_pdo pdos[PROTOCOL_PD_MAX_PDO_NUMS] = { 0 };
	int i, ret;

	ret = protocol_class_pd_get_pdos(current_port, pdos,
					 PROTOCOL_PD_MAX_PDO_NUMS);
	if (ret)
		return ret;
	caps->nums = 0;
	for (i = 0; i < PROTOCOL_PD_MAX_PDO_NUMS &&
		     caps->nums < ADAPTER_CAP_MAX_NR; i++) {
		if (!pdos[i].max_volt || pdos[i].min_volt != pdos[i].max_volt)
			continue;
		caps->cap[caps->nums].min_voltage = pdos[i].min_volt;
		caps->cap[caps->nums].max_voltage = pdos[i].max_volt;
		caps->cap[caps->nums].max_current = pdos[i].max_current;
		caps->cap[caps->nums].max_power =
			pdos[i].max_volt * pdos[i].max_current / 1000;
		caps->nums++;
	}
	return caps->nums ? 0 : -ENODATA;
}

static int pps_adapter_get_caps(void *data, struct adapter_power_cap_info *caps)
{
	struct pd_pdo pdos[PROTOCOL_PD_MAX_PDO_NUMS] = { 0 };
	int i, ret;

	ret = protocol_class_pd_get_pdos(current_port, pdos,
					 PROTOCOL_PD_MAX_PDO_NUMS);
	if (ret)
		return ret;
	caps->nums = 0;
	for (i = 0; i < PROTOCOL_PD_MAX_PDO_NUMS &&
		     caps->nums < ADAPTER_CAP_MAX_NR; i++) {
		if (!pdos[i].max_volt || pdos[i].min_volt == pdos[i].max_volt)
			continue;
		caps->cap[caps->nums].min_voltage = pdos[i].min_volt;
		caps->cap[caps->nums].max_voltage = pdos[i].max_volt;
		caps->cap[caps->nums].max_current = pdos[i].max_current;
		caps->cap[caps->nums].max_power =
			pdos[i].max_volt * pdos[i].max_current / 1000;
		caps->nums++;
	}
	return caps->nums ? 0 : -ENODATA;
}

static int pd_adapter_get_max_power(void *data, unsigned int *max_power)
{
	struct adapter_power_cap_info caps = { 0 };
	unsigned int max = 0;
	int i, ret;

	ret = pd_adapter_get_fixed_caps(data, &caps);
	if (ret)
		return ret;
	for (i = 0; i < caps.nums; i++)
		max = max_t(unsigned int, max, caps.cap[i].max_power);
	*max_power = max;
	return 0;
}

static int pps_adapter_get_max_power(void *data, unsigned int *max_power)
{
	int ret;
	struct adapter_power_cap_info caps = { 0 };
	unsigned int max = 0;
	int i;

	ret = protocol_class_pd_get_pps_max_power(current_port, max_power);
	if (!ret)
		return 0;
	ret = pps_adapter_get_caps(data, &caps);
	if (ret)
		return ret;
	for (i = 0; i < caps.nums; i++)
		max = max_t(unsigned int, max, caps.cap[i].max_power);
	*max_power = max;
	return 0;
}

static int pd_adapter_set_volt_curr(void *data, int volt, int curr)
{
	return protocol_class_pd_set_pps_pdo_select(current_port, volt, curr);
}

static int pps_adapter_get_volt_curr(void *data, int *volt, int *curr)
{
	return protocol_class_pd_get_pps_status(current_port, volt, curr);
}

static int pps_adapter_get_ptf(void *data, int *ptf)
{
	return protocol_class_pd_get_pps_ptf(current_port, ptf);
}

static int pd_adapter_get_info(void *data, struct adapter_vendor_info *info)
{
	int ret_id, ret_svid;

	ret_id = protocol_class_pd_get_adapter_id(current_port, &info->pid);
	ret_svid = protocol_class_pd_get_adapter_svid(current_port, &info->vid);
	if (ret_id && ret_svid)
		return ret_id;
	return 0;
}

static struct adapter_protocol_class_ops pd_adapter_ops = {
	.set_adapter_verified = pd_adapter_set_verified,
	.get_adapter_verified = pd_adapter_get_verified,
	.get_adapter_type = pd_adapter_get_type,
	.get_adapter_max_power = pd_adapter_get_max_power,
	.get_adapter_pwr_cap = pd_adapter_get_fixed_caps,
	.set_adapter_volt_and_curr = pd_adapter_set_volt_curr,
	.get_adapter_pps_ptf = pps_adapter_get_ptf,
	.get_adapter_info = pd_adapter_get_info,
};

static struct adapter_protocol_class_ops pps_adapter_ops = {
	.get_adapter_max_power = pps_adapter_get_max_power,
	.get_adapter_pwr_max_power = pps_adapter_get_max_power,
	.get_adapter_pwr_cap = pps_adapter_get_caps,
	.set_adapter_volt_and_curr = pd_adapter_set_volt_curr,
	.get_adapter_volt_and_curr = pps_adapter_get_volt_curr,
	.get_adapter_pps_ptf = pps_adapter_get_ptf,
	.get_adapter_info = pd_adapter_get_info,
};

static int mca_protocol_pd_probe(struct platform_device *pdev)
{
	int ret;

	current_port = TYPEC_PORT_0;
	ret = protocol_class_register_ops(ADAPTER_PROTOCOL_PD,
					  &pd_adapter_ops, NULL);
	if (ret)
		return ret;
	return protocol_class_register_ops(ADAPTER_PROTOCOL_PPS,
					   &pps_adapter_ops, NULL);
}

static const struct of_device_id mca_protocol_pd_of_match[] = {
	{ .compatible = "mca,protocol_pd" },
	{},
};
MODULE_DEVICE_TABLE(of, mca_protocol_pd_of_match);

static struct platform_driver mca_protocol_pd_driver = {
	.driver = {
		.name = "protocol_pd_class",
		.of_match_table = mca_protocol_pd_of_match,
	},
	.probe = mca_protocol_pd_probe,
};
module_platform_driver(mca_protocol_pd_driver);

MODULE_DESCRIPTION("Xiaomi MCA PD/PPS protocol bridge");
MODULE_LICENSE("GPL v2");
