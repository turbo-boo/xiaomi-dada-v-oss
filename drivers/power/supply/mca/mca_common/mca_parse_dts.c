// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_dts"
#endif

int mca_parse_dts_u8(const struct device_node *np, const char *prop, u8 *data,
		     u8 default_value)
{
	if (!np || !prop || !data)
		return -EINVAL;
	if (of_property_read_u8(np, prop, data)) {
		*data = default_value;
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(mca_parse_dts_u8);

int mca_parse_dts_u32(const struct device_node *np, const char *prop, u32 *data,
		      u32 default_value)
{
	if (!np || !prop || !data)
		return -EINVAL;
	if (of_property_read_u32(np, prop, data)) {
		*data = default_value;
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(mca_parse_dts_u32);

int mca_parse_dts_u8_array(const struct device_node *np, const char *prop,
			   u8 *data, u16 len)
{
	if (!np || !prop || !data)
		return -EINVAL;
	return of_property_read_u8_array(np, prop, data, len);
}
EXPORT_SYMBOL(mca_parse_dts_u8_array);

int mca_parse_dts_u32_array(const struct device_node *np, const char *prop,
			    u32 *data, u32 len)
{
	if (!np || !prop || !data)
		return -EINVAL;
	return of_property_read_u32_array(np, prop, data, len);
}
EXPORT_SYMBOL(mca_parse_dts_u32_array);

int mca_parse_dts_u8_count(const struct device_node *np, const char *prop,
			   u32 row, u32 col)
{
	int len;

	if (!np || !prop || !col)
		return -EINVAL;
	len = of_property_count_u8_elems(np, prop);
	if (len <= 0 || (unsigned int)len % col ||
	    (unsigned int)len > row * col)
		return -EINVAL;
	return len;
}
EXPORT_SYMBOL(mca_parse_dts_u8_count);

int mca_parse_dts_u32_count(const struct device_node *np, const char *prop,
			    u32 row, u32 col)
{
	int len;

	if (!np || !prop || !col)
		return -EINVAL;
	len = of_property_count_u32_elems(np, prop);
	if (len <= 0 || (unsigned int)len % col ||
	    (unsigned int)len > row * col)
		return -EINVAL;
	return len;
}
EXPORT_SYMBOL(mca_parse_dts_u32_count);

int mca_parse_dts_u32_index(const struct device_node *np, const char *prop,
			    int index, u32 *data)
{
	if (!np || !prop || !data)
		return -EINVAL;
	return of_property_read_u32_index(np, prop, index, data);
}
EXPORT_SYMBOL(mca_parse_dts_u32_index);

int mca_parse_dts_string(const struct device_node *np, const char *prop,
			 const char **out)
{
	if (!np || !prop || !out)
		return -EINVAL;
	return of_property_read_string(np, prop, out);
}
EXPORT_SYMBOL(mca_parse_dts_string);

int mca_parse_dts_string_index(const struct device_node *np, const char *prop,
			       int index, const char **out)
{
	if (!np || !prop || !out)
		return -EINVAL;
	return of_property_read_string_index(np, prop, index, out);
}
EXPORT_SYMBOL(mca_parse_dts_string_index);

int mca_parse_dts_count_strings(const struct device_node *np, const char *prop,
				u32 row, u32 col)
{
	int len;

	if (!np || !prop || !col)
		return -EINVAL;
	len = of_property_count_strings(np, prop);
	if (len <= 0 || (unsigned int)len % col ||
	    (unsigned int)len > row * col)
		return -EINVAL;
	return len;
}
EXPORT_SYMBOL(mca_parse_dts_count_strings);

int mca_parse_dts_string_array(const struct device_node *np, const char *prop,
			       int *data, u32 row, u32 col)
{
	int i, len;
	const char *tmp;

	if (!data)
		return -EINVAL;
	len = mca_parse_dts_count_strings(np, prop, row, col);
	if (len < 0)
		return len;
	for (i = 0; i < len; i++) {
		if (of_property_read_string_index(np, prop, i, &tmp))
			return -EINVAL;
		if (kstrtoint(tmp, 0, &data[i]))
			return -EINVAL;
	}
	return len;
}
EXPORT_SYMBOL(mca_parse_dts_string_array);

MODULE_DESCRIPTION("Xiaomi MCA DT parsing helpers");
MODULE_LICENSE("GPL v2");
