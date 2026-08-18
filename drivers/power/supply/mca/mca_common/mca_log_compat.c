// SPDX-License-Identifier: GPL-2.0
/*
 * Temporary Dada MCA logging backend.
 *
 * The full Xiaomi logger depends on MCA event/sysfs layers which are restored
 * later in the port.  Keep the public MCA logging ABI available so early core
 * components can be built and tested independently.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/stdarg.h>
#include <mca/common/mca_log.h>

static uint charge_boot_mode;
module_param(charge_boot_mode, uint, 0444);
MODULE_PARM_DESC(charge_boot_mode, "charge_boot_mode");

struct mca_charge_log_registration {
	struct mca_log_charge_log_ops *ops;
	void *data;
};

static struct mca_charge_log_registration
	charge_log_registry[MCA_CHARGE_LOG_ID_MAX];

static void mca_log_vprintk(const char *level, const char *format, va_list args)
{
	struct va_format vaf = {
		.fmt = format,
		.va = &args,
	};

	printk("%s%pV", level, &vaf);
}

void __mca_log_err(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	mca_log_vprintk(KERN_ERR, format, args);
	va_end(args);
}
EXPORT_SYMBOL(__mca_log_err);

void __mca_log_info(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	mca_log_vprintk(KERN_INFO, format, args);
	va_end(args);
}
EXPORT_SYMBOL(__mca_log_info);

void __mca_log_debug(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	mca_log_vprintk(KERN_DEBUG, format, args);
	va_end(args);
}
EXPORT_SYMBOL(__mca_log_debug);

void mca_log_charge_log_register(enum mca_charge_log_id_ele type,
				 struct mca_log_charge_log_ops *ops, void *data)
{
	if (type < 0 || type >= MCA_CHARGE_LOG_ID_MAX)
		return;

	charge_log_registry[type].ops = ops;
	charge_log_registry[type].data = data;
}
EXPORT_SYMBOL(mca_log_charge_log_register);

int mca_log_get_charge_boot_mode(void)
{
	return charge_boot_mode;
}
EXPORT_SYMBOL(mca_log_get_charge_boot_mode);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Dada MCA temporary logging backend");
