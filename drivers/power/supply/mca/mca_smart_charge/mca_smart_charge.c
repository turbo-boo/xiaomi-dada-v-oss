// SPDX-License-Identifier: GPL-2.0
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/refcount.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/smartchg/smart_chg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_smart_charge"
#endif

#define MCA_SMARTCHG_MAGIC 0x10
#define MCA_SMARTCHG_CMD_DATA 1
#define SMARTCHG_DATA_CMD \
	_IOC(_IOC_READ | _IOC_WRITE, MCA_SMARTCHG_MAGIC, \
	     MCA_SMARTCHG_CMD_DATA, PAGE_SIZE)
#define SMARTCHG_DATA_SIZE PAGE_SIZE

enum smartchg_attr_id {
	MCA_PROP_SMARTCHG = 0,
	MCA_PROP_SMARTCHG_FV,
	MCA_PROP_SMARTCHG_ICHG,
	MCA_PROP_SMARTBATT,
	MCA_PROP_SMARTNIGHT,
	MCA_PROP_POSTURE,
	MCA_PROP_SCENE,
	MCA_PROP_BOARD_TEMP,
	MCA_PROP_SMART_SIC_MODE,
};

struct smart_charge_info {
	struct device *dev;
	struct mutex lock;
	dev_t dev_num;
	struct cdev pri_dev;
	struct class *smart_charge_class;
	struct device *cdev_device;
	int cell_type;
	int enable_fv_dec_by_cc;
	int smart_chg;
	int delta_fv;
	int delta_ichg;
	int smart_batt;
	int smart_night;
	int posture;
	int scene;
	int board_temp;
	int smart_sic_mode;
	int limit_soc;
	bool extreme_cold_enabled;
};

struct smart_charge_file_ctx {
	struct smart_charge_info *info;
	void *buf;
	refcount_t refs;
	bool data_ready;
	bool mapped;
};

static struct smart_charge_info *global_smartchg_info;
static struct mca_smartchg_if_ops *g_mca_smartchg_if_ops[MCA_SMARTCHG_IF_CHG_TYPE_END];
static DEFINE_MUTEX(mca_smartchg_ops_lock);

static ssize_t smart_charge_sysfs_show(struct device *dev,
				       struct device_attribute *attr, char *buf);
static ssize_t smart_charge_sysfs_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count);

static struct mca_sysfs_attr_info smartchg_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTCHG, smart_chg),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTCHG_FV, smart_fv),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTCHG_ICHG, smart_ichg),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTBATT, smart_batt),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTNIGHT, smart_night),
	mca_sysfs_attr_ro(smart_charge_sysfs, 0440, MCA_PROP_POSTURE, posture),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SCENE, scene),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_BOARD_TEMP, board_temp),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMART_SIC_MODE, smart_sic_mode),
};

#define SMARTCHG_SYSFS_ATTRS_SIZE ARRAY_SIZE(smartchg_sysfs_field_tbl)
static struct attribute *smartchg_sysfs_attrs[SMARTCHG_SYSFS_ATTRS_SIZE + 1];
static const struct attribute_group smartchg_sysfs_attr_group = {
	.attrs = smartchg_sysfs_attrs,
};

int mca_smartchg_if_ops_register(struct mca_smartchg_if_ops *ops)
{
	if (!ops || ops->type < 0 || ops->type >= MCA_SMARTCHG_IF_CHG_TYPE_END)
		return -EINVAL;
	mutex_lock(&mca_smartchg_ops_lock);
	g_mca_smartchg_if_ops[ops->type] = ops;
	mutex_unlock(&mca_smartchg_ops_lock);
	return 0;
}
EXPORT_SYMBOL(mca_smartchg_if_ops_register);

static struct mca_smartchg_if_ops *mca_smartchg_if_get_ops(unsigned int type)
{
	struct mca_smartchg_if_ops *ops = NULL;

	if (type >= MCA_SMARTCHG_IF_CHG_TYPE_END)
		return NULL;
	mutex_lock(&mca_smartchg_ops_lock);
	ops = g_mca_smartchg_if_ops[type];
	mutex_unlock(&mca_smartchg_ops_lock);
	return ops;
}

static void mca_smartchg_apply_delta_fv(int value)
{
	int i;

	mutex_lock(&mca_smartchg_ops_lock);
	for (i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
		struct mca_smartchg_if_ops *ops = g_mca_smartchg_if_ops[i];

		if (ops && ops->set_delta_fv)
			ops->set_delta_fv(ops->data, value);
	}
	mutex_unlock(&mca_smartchg_ops_lock);
}

static void mca_smartchg_apply_delta_ichg(int value)
{
	int i;

	mutex_lock(&mca_smartchg_ops_lock);
	for (i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
		struct mca_smartchg_if_ops *ops = g_mca_smartchg_if_ops[i];

		if (ops && ops->set_delta_ichg)
			ops->set_delta_ichg(ops->data, value);
	}
	mutex_unlock(&mca_smartchg_ops_lock);
}

void mca_smartchg_set_scene(int scene)
{
	struct smart_charge_info *info = global_smartchg_info;

	if (!info)
		return;
	mutex_lock(&info->lock);
	info->scene = scene;
	mutex_unlock(&info->lock);
}
EXPORT_SYMBOL(mca_smartchg_set_scene);

int mca_smartchg_get_scene(void)
{
	struct smart_charge_info *info = global_smartchg_info;
	int scene = 0;

	if (!info)
		return 0;
	mutex_lock(&info->lock);
	scene = info->scene;
	mutex_unlock(&info->lock);
	return scene;
}
EXPORT_SYMBOL(mca_smartchg_get_scene);

void mca_smartchg_set_board_temp(int board_temp)
{
	struct smart_charge_info *info = global_smartchg_info;

	if (!info)
		return;
	mutex_lock(&info->lock);
	info->board_temp = board_temp;
	mutex_unlock(&info->lock);
}
EXPORT_SYMBOL(mca_smartchg_set_board_temp);

int mca_smartchg_get_board_temp(void)
{
	struct smart_charge_info *info = global_smartchg_info;
	int value = 0;

	if (!info)
		return 0;
	mutex_lock(&info->lock);
	value = info->board_temp;
	mutex_unlock(&info->lock);
	return value;
}
EXPORT_SYMBOL(mca_smartchg_get_board_temp);

int mca_smartchg_is_extreme_cold_enabled(void)
{
	return global_smartchg_info ? global_smartchg_info->extreme_cold_enabled : 0;
}
EXPORT_SYMBOL(mca_smartchg_is_extreme_cold_enabled);

int mca_smartchg_get_limit_soc(void)
{
	return global_smartchg_info ? global_smartchg_info->limit_soc : 0;
}
EXPORT_SYMBOL(mca_smartchg_get_limit_soc);

static ssize_t smart_charge_sysfs_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	struct smart_charge_info *info = dev_get_drvdata(dev);
	int value = 0;

	if (!info)
		return -ENODEV;
	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  smartchg_sysfs_field_tbl,
					  SMARTCHG_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -EINVAL;

	mutex_lock(&info->lock);
	switch (attr_info->sysfs_attr_name) {
	case MCA_PROP_SMARTCHG:
		value = info->smart_chg;
		break;
	case MCA_PROP_SMARTCHG_FV:
		value = info->delta_fv;
		break;
	case MCA_PROP_SMARTCHG_ICHG:
		value = info->delta_ichg;
		break;
	case MCA_PROP_SMARTBATT:
		value = info->smart_batt;
		break;
	case MCA_PROP_SMARTNIGHT:
		value = info->smart_night;
		break;
	case MCA_PROP_POSTURE:
		value = info->posture;
		break;
	case MCA_PROP_SCENE:
		value = info->scene;
		break;
	case MCA_PROP_BOARD_TEMP:
		value = info->board_temp;
		break;
	case MCA_PROP_SMART_SIC_MODE:
		value = info->smart_sic_mode;
		break;
	default:
		mutex_unlock(&info->lock);
		return -EINVAL;
	}
	mutex_unlock(&info->lock);
	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t smart_charge_sysfs_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	struct smart_charge_info *info = dev_get_drvdata(dev);
	int value, apply_fv = 0, apply_ichg = 0;

	if (!info)
		return -ENODEV;
	if (kstrtoint(buf, 0, &value))
		return -EINVAL;
	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  smartchg_sysfs_field_tbl,
					  SMARTCHG_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -EINVAL;

	mutex_lock(&info->lock);
	switch (attr_info->sysfs_attr_name) {
	case MCA_PROP_SMARTCHG:
		info->smart_chg = value;
		break;
	case MCA_PROP_SMARTCHG_FV:
		info->delta_fv = value * max(info->cell_type, 1);
		apply_fv = 1;
		value = info->delta_fv;
		break;
	case MCA_PROP_SMARTCHG_ICHG:
		info->delta_ichg = value;
		apply_ichg = 1;
		break;
	case MCA_PROP_SMARTBATT:
		info->smart_batt = value;
		break;
	case MCA_PROP_SMARTNIGHT:
		info->smart_night = value;
		break;
	case MCA_PROP_SCENE:
		info->scene = value;
		break;
	case MCA_PROP_BOARD_TEMP:
		info->board_temp = value;
		break;
	case MCA_PROP_SMART_SIC_MODE:
		info->smart_sic_mode = value;
		break;
	default:
		mutex_unlock(&info->lock);
		return -EACCES;
	}
	mutex_unlock(&info->lock);

	if (apply_fv)
		mca_smartchg_apply_delta_fv(value);
	if (apply_ichg)
		mca_smartchg_apply_delta_ichg(value);
	return count;
}

static int smart_charge_validate_specs(const char *buf, size_t total_len,
				       u32 count, size_t *offset)
{
	u32 i;

	for (i = 0; i < count; i++) {
		const struct smart_batt_spec *spec;
		size_t curves_size, record_size, end;

		if (*offset > total_len ||
		    total_len - *offset < offsetof(struct smart_batt_spec, steps))
			return -EINVAL;
		spec = (const struct smart_batt_spec *)(buf + *offset);
		if (check_mul_overflow((size_t)spec->step_size,
				       sizeof(struct smart_batt_spec_curve),
				       &curves_size) ||
		    check_add_overflow(offsetof(struct smart_batt_spec, steps),
				       curves_size, &record_size) ||
		    check_add_overflow(*offset, record_size, &end) ||
		    end > total_len)
			return -EINVAL;
		*offset = end;
	}
	return 0;
}

static int smart_charge_validate_basp(const char *buf,
				      const struct smart_basp_header **header_out)
{
	const struct smart_basp_header *h;
	u32 checksum = 0;
	u64 jeita_count;
	size_t jeita_bytes, offset, wls_end;
	unsigned int i;
	int ret;

	if (!buf || !header_out)
		return -EINVAL;
	h = (const struct smart_basp_header *)buf;
	if (h->total_len < sizeof(*h) || h->total_len > SMARTCHG_DATA_SIZE)
		return -EINVAL;

	for (i = sizeof(h->checksum); i < h->total_len; i++)
		checksum += (u8)buf[i];
	if (checksum != h->checksum)
		return -EBADMSG;

	jeita_count = (u64)h->jeita_ffc_term_size + h->jeita_normal_term_size;
	if (jeita_count > SIZE_MAX / sizeof(struct smart_batt_jeita_term_para))
		return -EOVERFLOW;
	jeita_bytes = (size_t)jeita_count * sizeof(struct smart_batt_jeita_term_para);
	if (check_add_overflow(sizeof(*h), jeita_bytes, &offset) ||
	    offset > h->total_len)
		return -EINVAL;

	ret = smart_charge_validate_specs(buf, h->total_len,
					 h->wired_ffc_size, &offset);
	if (ret)
		return ret;
	ret = smart_charge_validate_specs(buf, h->total_len,
					 h->wired_normal_size, &offset);
	if (ret)
		return ret;

	wls_end = offset;
	ret = smart_charge_validate_specs(buf, h->total_len,
					 h->wls_ffc_size, &wls_end);
	if (ret)
		return ret;
	ret = smart_charge_validate_specs(buf, h->total_len,
					 h->wls_normal_size, &wls_end);
	if (ret)
		return ret;
	if (wls_end > h->total_len)
		return -EINVAL;

	*header_out = h;
	return 0;
}

static int smart_charge_handle_baa_data(struct smart_charge_info *info,
					const char *buf)
{
	const struct smart_basp_header *h;
	struct mca_smartchg_if_ops *ops;
	size_t offset;
	int ret;

	ret = smart_charge_validate_basp(buf, &h);
	if (ret) {
		mca_log_err("invalid BASP payload: %d\n", ret);
		return ret;
	}
	if (!info->enable_fv_dec_by_cc)
		return 0;

	offset = sizeof(*h);
	ops = mca_smartchg_if_get_ops(MCA_SMARTCHG_IF_CHG_TYPE_JEITA);
	if (ops && ops->update_baa_para)
		ops->update_baa_para(ops->data, (char *)buf + offset,
				     h->jeita_ffc_term_size,
				     h->jeita_normal_term_size);
	offset += ((size_t)h->jeita_ffc_term_size + h->jeita_normal_term_size) *
		  sizeof(struct smart_batt_jeita_term_para);

	ops = mca_smartchg_if_get_ops(MCA_SMARTCHG_IF_CHG_TYPE_QC);
	if (ops && ops->update_baa_para)
		ops->update_baa_para(ops->data, (char *)buf + offset,
				     h->wired_ffc_size, h->wired_normal_size);
	ret = smart_charge_validate_specs(buf, h->total_len,
					 h->wired_ffc_size, &offset);
	if (ret)
		return ret;
	ret = smart_charge_validate_specs(buf, h->total_len,
					 h->wired_normal_size, &offset);
	if (ret)
		return ret;

	ops = mca_smartchg_if_get_ops(MCA_SMARTCHG_IF_CHG_TYPE_WL_QC);
	if (ops && ops->update_baa_para)
		ops->update_baa_para(ops->data, (char *)buf + offset,
				     h->wls_ffc_size, h->wls_normal_size);

	mca_log_info("BASP applied type=%u len=%u jeita=%u/%u wired=%u/%u wls=%u/%u\n",
		     h->type, h->total_len, h->jeita_ffc_term_size,
		     h->jeita_normal_term_size, h->wired_ffc_size,
		     h->wired_normal_size, h->wls_ffc_size, h->wls_normal_size);
	return 0;
}

static void smart_charge_ctx_put(struct smart_charge_file_ctx *ctx)
{
	if (!ctx)
		return;
	if (refcount_dec_and_test(&ctx->refs)) {
		if (ctx->buf)
			free_page((unsigned long)ctx->buf);
		kfree(ctx);
	}
}

static void smart_charge_vma_open(struct vm_area_struct *vma)
{
	struct smart_charge_file_ctx *ctx = vma->vm_private_data;

	if (ctx)
		refcount_inc(&ctx->refs);
}

static void smart_charge_vma_close(struct vm_area_struct *vma)
{
	struct smart_charge_file_ctx *ctx = vma->vm_private_data;

	smart_charge_ctx_put(ctx);
}

static const struct vm_operations_struct smart_charge_vm_ops = {
	.open = smart_charge_vma_open,
	.close = smart_charge_vma_close,
};

static int smart_charge_file_open(struct inode *inode, struct file *filp)
{
	struct smart_charge_info *info =
		container_of(inode->i_cdev, struct smart_charge_info, pri_dev);
	struct smart_charge_file_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->buf = (void *)get_zeroed_page(GFP_KERNEL);
	if (!ctx->buf) {
		kfree(ctx);
		return -ENOMEM;
	}
	ctx->info = info;
	refcount_set(&ctx->refs, 1);
	filp->private_data = ctx;
	return 0;
}

static int smart_charge_file_close(struct inode *inode, struct file *filp)
{
	struct smart_charge_file_ctx *ctx = filp->private_data;

	if (!ctx)
		return 0;
	if (ctx->data_ready || ctx->mapped)
		smart_charge_handle_baa_data(ctx->info, ctx->buf);
	filp->private_data = NULL;
	smart_charge_ctx_put(ctx);
	return 0;
}

static int smart_charge_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct smart_charge_file_ctx *ctx = filp->private_data;
	unsigned long size = vma->vm_end - vma->vm_start;
	int ret;

	if (!ctx || !ctx->buf || size == 0 || size > SMARTCHG_DATA_SIZE)
		return -EINVAL;
	if (ctx->mapped)
		return -EBUSY;

	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_ops = &smart_charge_vm_ops;
	vma->vm_private_data = ctx;
	smart_charge_vma_open(vma);
	ret = remap_pfn_range(vma, vma->vm_start,
			      virt_to_phys(ctx->buf) >> PAGE_SHIFT,
			      size, vma->vm_page_prot);
	if (ret) {
		vma->vm_private_data = NULL;
		smart_charge_ctx_put(ctx);
		return ret;
	}
	ctx->mapped = true;
	return 0;
}

static long smart_charge_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct smart_charge_file_ctx *ctx = filp->private_data;

	if (cmd != SMARTCHG_DATA_CMD)
		return -EINVAL;
	if (!ctx || !ctx->buf)
		return -EINVAL;
	if (copy_from_user(ctx->buf, (void __user *)arg, SMARTCHG_DATA_SIZE))
		return -EFAULT;
	ctx->data_ready = true;
	return 0;
}

static const struct file_operations smart_charge_file_ops = {
	.owner = THIS_MODULE,
	.open = smart_charge_file_open,
	.release = smart_charge_file_close,
	.mmap = smart_charge_mmap,
	.unlocked_ioctl = smart_charge_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = smart_charge_ioctl,
#endif
};

static int smart_charge_cdev_init(struct smart_charge_info *info)
{
	int ret;

	ret = alloc_chrdev_region(&info->dev_num, 0, 1, "smart_charge");
	if (ret)
		return ret;
	cdev_init(&info->pri_dev, &smart_charge_file_ops);
	info->pri_dev.owner = THIS_MODULE;
	ret = cdev_add(&info->pri_dev, info->dev_num, 1);
	if (ret)
		goto err_region;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
	info->smart_charge_class = class_create(THIS_MODULE, "smart_charge");
#else
	info->smart_charge_class = class_create("smart_charge");
#endif
	if (IS_ERR(info->smart_charge_class)) {
		ret = PTR_ERR(info->smart_charge_class);
		info->smart_charge_class = NULL;
		goto err_cdev;
	}
	info->cdev_device = device_create(info->smart_charge_class, NULL,
					  info->dev_num, NULL,
					  "smart_charge_cdev");
	if (IS_ERR(info->cdev_device)) {
		ret = PTR_ERR(info->cdev_device);
		info->cdev_device = NULL;
		goto err_class;
	}
	return 0;

err_class:
	class_destroy(info->smart_charge_class);
	info->smart_charge_class = NULL;
err_cdev:
	cdev_del(&info->pri_dev);
err_region:
	unregister_chrdev_region(info->dev_num, 1);
	return ret;
}

static void smart_charge_cdev_exit(struct smart_charge_info *info)
{
	if (!info)
		return;
	if (info->cdev_device && info->smart_charge_class)
		device_destroy(info->smart_charge_class, info->dev_num);
	if (info->smart_charge_class)
		class_destroy(info->smart_charge_class);
	cdev_del(&info->pri_dev);
	unregister_chrdev_region(info->dev_num, 1);
	info->cdev_device = NULL;
	info->smart_charge_class = NULL;
}

static int smart_charge_probe(struct platform_device *pdev)
{
	struct smart_charge_info *info;
	u32 value = 1;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	mutex_init(&info->lock);
	if (!of_property_read_u32(pdev->dev.of_node, "cell_type", &value))
		info->cell_type = value ? value : 1;
	else
		info->cell_type = 1;
	value = 0;
	if (!of_property_read_u32(pdev->dev.of_node, "enable_fv_dec_by_cc", &value))
		info->enable_fv_dec_by_cc = value;
	info->extreme_cold_enabled =
		of_property_read_bool(pdev->dev.of_node, "support_extreme_cold");
	platform_set_drvdata(pdev, info);

	mca_sysfs_init_attrs(smartchg_sysfs_attrs, smartchg_sysfs_field_tbl,
			     SMARTCHG_SYSFS_ATTRS_SIZE);
	ret = mca_sysfs_create_link_group("charger", "smart_charge", &pdev->dev,
					  &smartchg_sysfs_attr_group);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to create smart_charge sysfs\n");
	ret = smart_charge_cdev_init(info);
	if (ret) {
		mca_sysfs_remove_link_group("charger", "smart_charge", &pdev->dev,
					    &smartchg_sysfs_attr_group);
		return dev_err_probe(&pdev->dev, ret,
				     "failed to create smart_charge_cdev\n");
	}

	global_smartchg_info = info;
	mca_log_info("smart-charge ready, cell_type=%d baa=%d cdev=%u:%u\n",
		     info->cell_type, info->enable_fv_dec_by_cc,
		     MAJOR(info->dev_num), MINOR(info->dev_num));
	return 0;
}

static int smart_charge_remove(struct platform_device *pdev)
{
	struct smart_charge_info *info = platform_get_drvdata(pdev);

	if (global_smartchg_info == info)
		global_smartchg_info = NULL;
	smart_charge_cdev_exit(info);
	mca_sysfs_remove_link_group("charger", "smart_charge", &pdev->dev,
				    &smartchg_sysfs_attr_group);
	return 0;
}

static const struct of_device_id smart_charge_match_table[] = {
	{ .compatible = "xiaomi,smart_charge" },
	{},
};
MODULE_DEVICE_TABLE(of, smart_charge_match_table);

static struct platform_driver smart_charge_driver = {
	.driver = {
		.name = "smart_charge",
		.of_match_table = smart_charge_match_table,
	},
	.probe = smart_charge_probe,
	.remove = smart_charge_remove,
};
module_platform_driver(smart_charge_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA smart-charge BASP/sysfs core");
MODULE_LICENSE("GPL v2");
