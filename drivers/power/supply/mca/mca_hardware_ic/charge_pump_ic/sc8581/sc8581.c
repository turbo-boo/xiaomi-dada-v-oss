// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi/Southchip SC8581/SC8585 MCA charge-pump driver.
 * Reconstructed from Xiaomi's public Onyx MCA source for Dada.
 */

#include <mca/platform/platform_cp_class.h>
#include "inc/sc8581_reg.h"
#include "inc/sc8581.h"
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <linux/version.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <linux/pm_wakeup.h>
#include <mca/common/mca_hwid.h>
#include "hwid.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "cp_sc8581"
#endif

#define MAX_LENGTH_BYTE 600
#define MAX_REG_COUNT 0x42
#define MCA_SET_OVPGATE_COUNT 10
#define EN_BUCK_PROJECT_PLATFORM_VER 3
#define EN_BUCK_PROJECT_HWID 0x10004
#define SC8581_EN_BUCK_CP_I2C_ADDR 0x6f
#define RANGE_MIN_MAX 2
#define SC8581_ADC_REG_BASE SC8581_REG_17
#define CP_DUMP_REG_LEN 16
#define MANUAL_GATE_MASK 0x38

static int cp_get_adc_data(struct sc8581_device *bq, int channel, u32 *result);
static int sc8581_init_protection(struct sc8581_device *cp,
				  int forward_work_mode);

static int cp_read_word(struct i2c_client *client, u8 reg, u16 *val)
{
	s32 ret = i2c_smbus_read_word_data(client, reg);

	if (ret < 0) {
		mca_log_info("i2c read word fail reg=0x%02x ret=%d\n", reg, ret);
		return ret;
	}
	*val = (u16)ret;
	return 0;
}

static int cp_read_byte(struct i2c_client *client, u8 reg, u8 *val)
{
	s32 ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0) {
		mca_log_info("i2c read byte fail reg=0x%02x ret=%d\n", reg, ret);
		return ret;
	}
	*val = (u8)ret;
	return 0;
}

static int cp_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
	s32 ret = i2c_smbus_write_byte_data(client, reg, val);

	if (ret < 0) {
		mca_log_info("i2c write byte fail reg=0x%02x ret=%d\n", reg, ret);
		return ret;
	}
	return 0;
}

static int cp_read_i2c_block_data(struct i2c_client *client, u8 reg,
				  unsigned short len, u8 *buf)
{
	if (!client || !buf)
		return -EINVAL;
	return i2c_smbus_read_i2c_block_data(client, reg, len, buf);
}

static int cp_update_bits(struct i2c_client *client, u8 reg, u8 mask, u8 val)
{
	u8 tmp;
	int ret;

	ret = cp_read_byte(client, reg, &tmp);
	if (ret)
		return ret;
	tmp &= ~mask;
	tmp |= val & mask;
	return cp_write_byte(client, reg, tmp);
}

#ifdef CONFIG_DEBUG_FS
enum cp_attr_list {
	CP_DEBUG_PROP_ADDRESS,
	CP_DEBUG_PROP_COUNT,
	CP_DEBUG_PROP_DATA,
};

static struct reg_context {
	int address;
	int count;
	int data;
} reg_info;

static ssize_t cp_debugfs_show(void *priv_data, char *buf)
{
	struct mca_debugfs_attr_data *attr_data = priv_data;
	struct mca_debugfs_attr_info *attr_info;
	struct sc8581_device *dev_data;
	u8 val = 0;
	ssize_t count = 0;
	int i, ret;

	if (!attr_data)
		return -EINVAL;
	attr_info = attr_data->attr_info;
	dev_data = attr_data->private;
	if (!dev_data || !attr_info)
		return -EINVAL;

	switch (attr_info->debugfs_attr_name) {
	case CP_DEBUG_PROP_ADDRESS:
		return scnprintf(buf, PAGE_SIZE, "%02x\n", reg_info.address);
	case CP_DEBUG_PROP_COUNT:
		return scnprintf(buf, PAGE_SIZE, "%x\n", reg_info.count);
	case CP_DEBUG_PROP_DATA:
		for (i = 0; i < reg_info.count && count < PAGE_SIZE; i++) {
			ret = cp_read_byte(dev_data->client, reg_info.address + i, &val);
			if (ret)
				return ret;
			count += scnprintf(buf + count, PAGE_SIZE - count,
					   "%02x: %02x\n", reg_info.address + i,
					   val);
		}
		break;
	}
	return count;
}

static ssize_t cp_debugfs_store(void *priv_data, const char *buf, size_t count)
{
	struct mca_debugfs_attr_data *attr_data = priv_data;
	struct mca_debugfs_attr_info *attr_info;
	struct sc8581_device *dev_data;
	int val;

	if (!attr_data)
		return -EINVAL;
	attr_info = attr_data->attr_info;
	dev_data = attr_data->private;
	if (!dev_data || !attr_info)
		return -EINVAL;
	if (kstrtoint(buf, 16, &val))
		return -EINVAL;

	switch (attr_info->debugfs_attr_name) {
	case CP_DEBUG_PROP_ADDRESS:
		reg_info.address = val;
		break;
	case CP_DEBUG_PROP_COUNT:
		reg_info.count = clamp_val(val, 1, MAX_REG_COUNT);
		break;
	case CP_DEBUG_PROP_DATA:
		if (cp_write_byte(dev_data->client, reg_info.address, val))
			return -EIO;
		break;
	}
	return count;
}

static struct mca_debugfs_attr_info cp_debugfs_field_tbl[] = {
	mca_debugfs_attr(cp_debugfs, 0664, CP_DEBUG_PROP_ADDRESS, address),
	mca_debugfs_attr(cp_debugfs, 0664, CP_DEBUG_PROP_COUNT, count),
	mca_debugfs_attr(cp_debugfs, 0600, CP_DEBUG_PROP_DATA, data),
};
#define CP_DEBUGFS_ATTRS_SIZE ARRAY_SIZE(cp_debugfs_field_tbl)
#endif

static int sc8581_enable_adc(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_ADC_ENABLE : SC8581_ADC_DISABLE)
		 << SC8581_ADC_EN_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_15,
			      SC8581_ADC_EN_MASK, val);
}

static int sc8581_enable_charge(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_CHG_ENABLE : SC8581_CHG_DISABLE)
		 << SC8581_CHG_EN_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_0B,
			      SC8581_CHG_EN_MASK, val);
}

static int sc8581_check_charge_enabled(struct sc8581_device *bq, bool *enabled)
{
	u8 val;
	int ret = cp_read_byte(bq->client, SC8581_REG_0B, &val);

	if (!ret)
		*enabled = !!(val & SC8581_CHG_EN_MASK);
	return ret;
}

static int sc8581_enable_qb(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_QB_ENABLE : SC8581_QB_DISABLE)
		 << SC8581_QB_EN_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_0B,
			      SC8581_QB_EN_MASK, val);
}

static int sc8581_disable_rcp(struct sc8581_device *bq, bool disable)
{
	u8 val = (!!disable) << SC8581_IBUS_RCP_DIS_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_40,
			      SC8581_IBUS_RCP_DIS_MASK, val);
}

static int sc8581_enable_batovp(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_BAT_OVP_ENABLE : SC8581_BAT_OVP_DISABLE)
		 << SC8581_BAT_OVP_DIS_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_01,
			      SC8581_BAT_OVP_DIS_MASK, val);
}

static int sc8581_set_batovp_th(struct sc8581_device *bq, int threshold)
{
	u8 val;

	threshold = max(threshold, SC8581_BAT_OVP_BASE);
	val = (threshold - SC8581_BAT_OVP_BASE) / SC8581_BAT_OVP_LSB;
	return cp_update_bits(bq->client, SC8581_REG_01, SC8581_BAT_OVP_MASK,
			      val << SC8581_BAT_OVP_SHIFT);
}

static int sc8581_enable_batocp(struct sc8581_device *bq, bool enable)
{
	u8 val;

	if (bq->chip_vendor == SC8585)
		return 0;
	val = (enable ? SC8581_BAT_OCP_ENABLE : SC8581_BAT_OCP_DISABLE)
		 << SC8581_BAT_OCP_DIS_SHIFT;
	return cp_update_bits(bq->client, SC8581_REG_02,
			      SC8581_BAT_OCP_DIS_MASK, val);
}

static int sc8581_set_batocp_th(struct sc8581_device *bq, int threshold)
{
	u8 val;

	if (bq->chip_vendor == SC8585)
		return 0;
	threshold = max(threshold, SC8581_BAT_OCP_BASE);
	val = (threshold - SC8581_BAT_OCP_BASE) / SC8581_BAT_OCP_LSB;
	return cp_update_bits(bq->client, SC8581_REG_02, SC8581_BAT_OCP_MASK,
			      val << SC8581_BAT_OCP_SHIFT);
}

static int sc8581_set_usbovp_th(struct sc8581_device *bq, int threshold)
{
	u8 val;

	if (bq->chip_vendor == SC8585 && threshold == 7500)
		val = SC8585_USB_OVP_7PV5;
	else if (bq->chip_vendor != SC8585 && threshold == 6500)
		val = SC8581_USB_OVP_6PV5;
	else
		val = (threshold - SC8581_USB_OVP_BASE) / SC8581_USB_OVP_LSB;

	return cp_update_bits(bq->client, SC8581_REG_03, SC8581_USB_OVP_MASK,
			      val << SC8581_USB_OVP_SHIFT);
}

static int sc8581_set_ovpgate_on_dg_set(struct sc8581_device *bq, int data)
{
	u8 val = (data ? SC8581_OVPGATE_ON_DG_128MS :
			 SC8581_OVPGATE_ON_DG_20MS) << SC8581_OVPGATE_ON_DG_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_03,
			      SC8581_OVPGATE_ON_DG_MASK, val);
}

static int sc8581_set_wpcovp_th(struct sc8581_device *bq, int threshold)
{
	u8 val;

	if (bq->chip_vendor == SC8585 && threshold == 7500)
		val = SC8585_WPC_OVP_7PV5;
	else if (bq->chip_vendor != SC8585 && threshold == 6500)
		val = SC8581_WPC_OVP_6PV5;
	else
		val = (threshold - SC8581_WPC_OVP_BASE) / SC8581_WPC_OVP_LSB;

	return cp_update_bits(bq->client, SC8581_REG_04, SC8581_WPC_OVP_MASK,
			      val << SC8581_WPC_OVP_SHIFT);
}

static int sc8581_set_busovp_th(struct sc8581_device *bq, int threshold)
{
	static const int sc8581_base[CP_MODE_DIV_MAX] = {
		SC8581_BUS_OVP_41MODE_BASE, SC8581_BUS_OVP_21MODE_BASE,
		SC8581_BUS_OVP_11MODE_BASE,
	};
	static const int sc8581_lsb[CP_MODE_DIV_MAX] = {
		SC8581_BUS_OVP_41MODE_LSB, SC8581_BUS_OVP_21MODE_LSB,
		SC8581_BUS_OVP_11MODE_LSB,
	};
	static const int sc8585_base[CP_MODE_DIV_MAX] = {
		SC8585_BUS_OVP_41MODE_BASE, SC8585_BUS_OVP_21MODE_BASE,
		SC8585_BUS_OVP_11MODE_BASE,
	};
	static const int sc8585_lsb[CP_MODE_DIV_MAX] = {
		SC8585_BUS_OVP_41MODE_LSB, SC8585_BUS_OVP_21MODE_LSB,
		SC8585_BUS_OVP_11MODE_LSB,
	};
	const int *base = bq->chip_vendor == SC8585 ? sc8585_base : sc8581_base;
	const int *lsb = bq->chip_vendor == SC8585 ? sc8585_lsb : sc8581_lsb;
	u8 mask = bq->chip_vendor == SC8585 ? SC8585_BUS_OVP_MASK :
		SC8581_BUS_OVP_MASK;
	u8 val;

	if (bq->work_mode >= CP_MODE_DIV_MAX)
		return -EINVAL;
	threshold = max(threshold, base[bq->work_mode]);
	val = (threshold - base[bq->work_mode]) / lsb[bq->work_mode];
	return cp_update_bits(bq->client, SC8581_REG_05, mask,
			      val << SC8581_BUS_OVP_SHIFT);
}

static int sc8581_set_outovp_th(struct sc8581_device *bq, int threshold)
{
	u8 val;

	threshold = max(threshold, SC8581_OUT_OVP_BASE);
	val = (threshold - SC8581_OUT_OVP_BASE) / SC8581_OUT_OVP_LSB;
	return cp_update_bits(bq->client, SC8581_REG_05, SC8581_OUT_OVP_MASK,
			      val << SC8581_OUT_OVP_SHIFT);
}

static int sc8581_enable_busocp(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_BUS_OCP_ENABLE : SC8581_BUS_OCP_DISABLE)
		 << SC8581_BUS_OCP_DIS_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_06,
			      SC8581_BUS_OCP_DIS_MASK, val);
}

static int sc8581_set_busocp_th(struct sc8581_device *bq, int threshold)
{
	int base = bq->chip_vendor == SC8585 ? SC8585_BUS_OCP_BASE :
		SC8581_BUS_OCP_BASE;
	int lsb = bq->chip_vendor == SC8585 ? SC8585_BUS_OCP_LSB :
		SC8581_BUS_OCP_LSB;
	u8 val;

	threshold = max(threshold, base);
	val = (threshold - base) / lsb;
	return cp_update_bits(bq->client, SC8581_REG_06, SC8581_BUS_OCP_MASK,
			      val << SC8581_BUS_OCP_SHIFT);
}

static int sc8581_enable_busucp(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_BUS_UCP_ENABLE : SC8581_BUS_UCP_DISABLE)
		 << SC8581_BUS_UCP_DIS_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_07,
			      SC8581_BUS_UCP_DIS_MASK, val);
}

static int sc8581_enable_pmid2outovp(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_PMID2OUT_OVP_ENABLE :
			 SC8581_PMID2OUT_OVP_DISABLE) << SC8581_PMID2OUT_OVP_DIS_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_08,
			      SC8581_PMID2OUT_OVP_DIS_MASK, val);
}

static int sc8581_set_pmid2outovp_th(struct sc8581_device *bq, int threshold)
{
	u8 val;

	threshold = max(threshold, SC8581_PMID2OUT_OVP_BASE);
	val = (threshold - SC8581_PMID2OUT_OVP_BASE) / SC8581_PMID2OUT_OVP_LSB;
	return cp_update_bits(bq->client, SC8581_REG_08,
			      SC8581_PMID2OUT_OVP_MASK,
			      val << SC8581_PMID2OUT_OVP_SHIFT);
}

static int sc8581_enable_pmid2outuvp(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_PMID2OUT_UVP_ENABLE :
			 SC8581_PMID2OUT_UVP_DISABLE) << SC8581_PMID2OUT_UVP_DIS_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_09,
			      SC8581_PMID2OUT_UVP_DIS_MASK, val);
}

static int sc8581_set_pmid2outuvp_th(struct sc8581_device *bq, int threshold)
{
	u8 val;

	threshold = max(threshold, SC8581_PMID2OUT_UVP_BASE);
	val = (threshold - SC8581_PMID2OUT_UVP_BASE) / SC8581_PMID2OUT_UVP_LSB;
	return cp_update_bits(bq->client, SC8581_REG_09,
			      SC8581_PMID2OUT_UVP_MASK,
			      val << SC8581_PMID2OUT_UVP_SHIFT);
}

static int sc8581_enable_batovp_alarm(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_BAT_OVP_ALM_ENABLE :
			 SC8581_BAT_OVP_ALM_DISABLE) << SC8581_BAT_OVP_ALM_DIS_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_6C,
			      SC8581_BAT_OVP_ALM_DIS_MASK, val);
}

static int sc8581_set_batovp_alarm_th(struct sc8581_device *bq, int threshold)
{
	u8 val;

	threshold = max(threshold, SC8581_BAT_OVP_ALM_BASE);
	val = (threshold - SC8581_BAT_OVP_ALM_BASE) / SC8581_BAT_OVP_ALM_LSB;
	return cp_update_bits(bq->client, SC8581_REG_6C,
			      SC8581_BAT_OVP_ALM_MASK,
			      val << SC8581_BAT_OVP_ALM_SHIFT);
}

static int sc8581_enable_busocp_alarm(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_BUS_OCP_ALM_ENABLE :
			 SC8581_BUS_OCP_ALM_DISABLE) << SC8581_BUS_OCP_ALM_DIS_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_6D,
			      SC8581_BUS_OCP_ALM_DIS_MASK, val);
}

static int sc8581_set_adc_scanrate(struct sc8581_device *bq, bool oneshot)
{
	u8 val = (oneshot ? SC8581_ADC_RATE_ONESHOT :
			 SC8581_ADC_RATE_CONTINOUS) << SC8581_ADC_RATE_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_15,
			      SC8581_ADC_RATE_MASK, val);
}

static int sc8581_get_adc_data(struct sc8581_device *bq, int channel,
			       u32 *result)
{
	u16 tmp, val;
	int ret;

	if (!result || channel < 0 || channel >= ADC_MAX_NUM)
		return -EINVAL;
	ret = cp_read_word(bq->client, SC8581_ADC_REG_BASE + (channel << 1),
			   &tmp);
	if (ret)
		return ret;
	val = ((tmp & 0xff) << 8) | (tmp >> 8);

	switch (channel) {
	case ADC_IBUS:
		val = bq->chip_vendor == SC8585 ?
			val * SC8585_IBUS_ADC_LSB / SC8585_CURRENT_SCALE :
			val * SC8581_IBUS_ADC_LSB / SC8581_CURRENT_SCALE;
		break;
	case ADC_VBUS:
		val = val * SC8581_VBUS_ADC_LSB / SC8581_VOLTAGE_SCALE;
		break;
	case ADC_VUSB:
		val = val * SC8581_VUSB_ADC_LSB / SC8581_VOLTAGE_SCALE;
		break;
	case ADC_VWPC:
		val = val * SC8581_VWPC_ADC_LSB / SC8581_VOLTAGE_SCALE;
		break;
	case ADC_VOUT:
		val = val * SC8581_VOUT_ADC_LSB / SC8581_VOLTAGE_SCALE;
		break;
	case ADC_VBAT:
		val = val * SC8581_VBAT_ADC_LSB / SC8581_VOLTAGE_SCALE;
		break;
	case ADC_IBAT:
		val = val * SC8581_IBAT_ADC_LSB / SC8581_IBAT_SCALE;
		break;
	case ADC_TBAT:
		val = val * SC8581_TSBAT_ADC_LSB / SC8581_TEMP_SCALE;
		break;
	case ADC_TDIE:
		val = val * SC8581_TDIE_ADC_LSB / SC8581_TDIE_SCALE;
		break;
	}
	*result = val;
	return 0;
}

static int sc8581_set_adc_scan(struct sc8581_device *bq, int channel,
			       bool enable)
{
	u8 reg, mask, shift, val;

	if (channel < 0 || channel >= ADC_MAX_NUM)
		return -EINVAL;
	if (channel == ADC_IBUS) {
		reg = SC8581_REG_15;
		shift = SC8581_IBUS_ADC_DIS_SHIFT;
		mask = SC8581_IBUS_ADC_DIS_MASK;
	} else {
		reg = SC8581_REG_16;
		shift = 8 - channel;
		mask = BIT(shift);
	}
	val = enable ? 0 : BIT(shift);
	return cp_update_bits(bq->client, reg, mask, val);
}

static int sc8581_enable_parallel_func(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_SYNC_FUNCTION_ENABLE :
			 SC8581_SYNC_FUNCTION_DISABLE) << SC8581_SYNC_FUNCTION_EN_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_0E,
			      SC8581_SYNC_FUNCTION_EN_MASK, val);
}

static int sc8581_set_reg_reset(struct sc8581_device *bq)
{
	return cp_update_bits(bq->client, SC8581_REG_0E, SC8581_REG_RST_MASK,
			      SC8581_REG_RESET << SC8581_REG_RST_SHIFT);
}

static int sc8581_set_operation_mode(struct sc8581_device *bq,
				     int operation_mode)
{
	u8 val;
	int ret;

	switch (operation_mode) {
	case SC8581_FORWARD_4_1_CHARGER_MODE:
	case SC8581_REVERSE_1_4_CONVERTER_MODE:
		bq->work_mode = CP_MODE_DIV4;
		break;
	case SC8581_FORWARD_2_1_CHARGER_MODE:
	case SC8581_REVERSE_1_2_CONVERTER_MODE:
		bq->work_mode = CP_MODE_DIV2;
		break;
	case SC8581_FORWARD_1_1_CHARGER_MODE:
	case SC8581_REVERSE_1_1_CONVERTER_MODE:
	case SC8581_FORWARD_1_1_CHARGER_MODE_REVERSEED:
	case SC8581_REVERSE_1_1_CONVERTER_MODE_REVERSED:
		bq->work_mode = CP_MODE_DIV1;
		break;
	default:
		return -EINVAL;
	}

	val = operation_mode;
	if (operation_mode == SC8581_FORWARD_1_1_CHARGER_MODE_REVERSEED)
		val = SC8581_FORWARD_1_1_CHARGER_MODE;
	else if (operation_mode == SC8581_REVERSE_1_1_CONVERTER_MODE_REVERSED)
		val = SC8581_REVERSE_1_1_CONVERTER_MODE;
	bq->operation_mode = val;
	ret = sc8581_init_protection(bq, bq->work_mode);
	if (ret)
		return ret;
	return cp_update_bits(bq->client, SC8581_REG_0E, SC8581_MODE_MASK,
			      val << SC8581_MODE_SHIFT);
}

static int sc8581_get_operation_mode(struct sc8581_device *bq, int *mode)
{
	u8 val;
	int ret = cp_read_byte(bq->client, SC8581_REG_0E, &val);

	if (!ret)
		*mode = val & SC8581_MODE_MASK;
	return ret;
}

static int sc8581_get_int_stat(struct sc8581_device *bq, int channel,
			       bool *enable)
{
	u8 val;
	int ret;

	if (!enable)
		return -EINVAL;
	ret = cp_read_byte(bq->client, SC8581_REG_10, &val);
	if (ret)
		return ret;
	switch (channel) {
	case VOUT_OK_REV_STAT:
		*enable = !!(val & SC8581_VOUT_OK_REV_STAT_MASK); break;
	case VOUT_OK_CHG_STAT:
		*enable = !!(val & SC8581_VOUT_OK_CHG_STAT_MASK); break;
	case VOUT_INSERT_STAT:
		*enable = !!(val & SC8581_VOUT_INSERT_STAT_MASK); break;
	case VBUS_PRESENT_STAT:
		*enable = !!(val & SC8581_VBUS_PRESENT_STAT_MASK); break;
	case VWPC_PRESENT_STAT:
		*enable = !!(val & SC8581_VWPC_INSERT_STAT_MASK); break;
	case VUSB_PRESENT_STAT:
		*enable = !!(val & SC8581_VUSB_INSERT_STAT_MASK); break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int sc8581_enable_acdrv_manual(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_ACDRV_MANUAL_MODE :
			 SC8581_ACDRV_AUTO_MODE) << SC8581_ACDRV_MANUAL_EN_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_0B,
			      SC8581_ACDRV_MANUAL_EN_MASK, val);
}

static int sc8581_enable_wpcgate(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_WPCGATE_ENABLE : SC8581_WPCGATE_DISABLE)
		 << SC8581_WPCGATE_EN_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_0B,
			      SC8581_WPCGATE_EN_MASK, val);
}

static int sc8581_enable_ovpgate(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_OVPGATE_ENABLE : SC8581_OVPGATE_DISABLE)
		 << SC8581_OVPGATE_EN_SHIFT;
	int i, ret;
	u8 value;

	bq->ovpgate_en = enable;
	if (!bq->i2c_is_working)
		msleep(30);
	for (i = 0; i < MCA_SET_OVPGATE_COUNT; i++) {
		ret = cp_update_bits(bq->client, SC8581_REG_0B,
				     SC8581_OVPGATE_EN_MASK, val);
		if (ret)
			continue;
		ret = cp_read_byte(bq->client, SC8581_REG_0B, &value);
		if (!ret && !!(value & SC8581_OVPGATE_EN_MASK) == enable)
			return 0;
		msleep(10);
	}
	return -EIO;
}

static int sc8581_get_ovpgate_status(struct sc8581_device *bq, bool *enable)
{
	u8 val;
	int ret = cp_read_byte(bq->client, SC8581_REG_0F, &val);

	if (!ret)
		*enable = !!(val & SC8581_OVPGATE_STAT_MASK);
	return ret;
}

static int sc8581_enable_acdrv_manual_ovpgate_wpcgate(struct sc8581_device *bq,
						      bool enable)
{
	u8 val = (enable << SC8581_OVPGATE_EN_SHIFT) |
		 (enable << SC8581_WPCGATE_EN_SHIFT) |
		 (enable << SC8581_ACDRV_MANUAL_EN_SHIFT);

	return cp_update_bits(bq->client, SC8581_REG_0B, MANUAL_GATE_MASK, val);
}

static int sc8581_set_sense_resistor(struct sc8581_device *bq, int r_mohm)
{
	u8 val;

	if (r_mohm == 1)
		val = SC8581_IBAT_SNS_RES_1MHM;
	else if (r_mohm == 2)
		val = SC8581_IBAT_SNS_RES_2MHM;
	else
		return -EINVAL;
	return cp_update_bits(bq->client, SC8581_REG_0E,
			      SC8581_IBAT_SNS_RES_MASK,
			      val << SC8581_IBAT_SNS_RES_SHIFT);
}

static int sc8581_set_ss_timeout(struct sc8581_device *bq, u8 val)
{
	if (val > SC8581_SS_TIMEOUT_81920MS)
		val = SC8581_SS_TIMEOUT_DISABLE;
	return cp_update_bits(bq->client, SC8581_REG_0D,
			      SC8581_SS_TIMEOUT_SET_MASK,
			      val << SC8581_SS_TIMEOUT_SET_SHIFT);
}

static int sc8581_set_batovp_alarm_int_mask(struct sc8581_device *bq, u8 mask)
{
	u8 val;
	int ret = cp_read_byte(bq->client, SC8581_REG_6C, &val);

	if (ret)
		return ret;
	val |= mask << SC8581_BAT_OVP_ALM_MASK_SHIFT;
	return cp_write_byte(bq->client, SC8581_REG_6C, val);
}

static int sc8581_set_busocp_alarm_int_mask(struct sc8581_device *bq, u8 mask)
{
	u8 val;
	int ret = cp_read_byte(bq->client, SC8581_REG_6D, &val);

	if (ret)
		return ret;
	val |= mask << SC8581_BUS_OCP_ALM_MASK_SHIFT;
	return cp_write_byte(bq->client, SC8581_REG_6D, val);
}

static int sc8581_set_ucp_fall_dg(struct sc8581_device *bq, u8 data)
{
	return cp_update_bits(bq->client, SC8581_REG_07,
			      SC8581_BUS_UCP_FALL_DG_MASK,
			      data << SC8581_BUS_UCP_FALL_DG_SHIFT);
}

static int sc8581_set_enable_tsbat(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_TSBAT_ENABLE : SC8581_TSBAT_DISABLE)
		 << SC8581_TSBAT_EN_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_70,
			      SC8581_TSBAT_EN_MASK, val);
}

static int sc8581_tsbat_flt_dis(struct sc8581_device *bq, bool disable)
{
	u8 val = (disable ? SC8581_TSBAT_FLT_DISABLE : SC8581_TSBAT_FLT_ENABLE)
		 << SC8581_TSBAT_FLT_DIS_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_0F,
			      SC8581_TSBAT_FLT_DIS_MASK, val);
}

static int sc8581_pin_diag_dis(struct sc8581_device *bq, bool disable)
{
	u8 val = (disable ? SC8581_PIN_DIAG_DIS_DISABLE :
			 SC8581_PIN_DIAG_DIS_ENABLE) << SC8581_PIN_DIAG_DIS_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_7C,
			      SC8581_PIN_DIAG_DIS_MASK, val);
}

static int sc8581_set_sync(struct sc8581_device *bq, u8 data)
{
	return cp_update_bits(bq->client, SC8581_REG_0C, SC8581_SYNC_MASK,
			      data << SC8581_SYNC_SHIFT);
}

static int sc8581_set_switch_freq(struct sc8581_device *bq, u8 data)
{
	u8 mask = bq->chip_vendor == SC8585 ? SC8585_FSW_SET_MASK :
		SC8581_FSW_SET_MASK;

	return cp_update_bits(bq->client, SC8581_REG_0C, mask,
			      data << SC8581_FSW_SET_SHIFT);
}

static int sc8581_set_fsw(struct sc8581_device *cp, int fsw)
{
	int val;

	fsw = clamp(fsw, cp->fsw_cfg.min, cp->fsw_cfg.max);
	val = (fsw - cp->fsw_cfg.min) / cp->fsw_cfg.step;
	return sc8581_set_switch_freq(cp, val);
}

static int sc8581_get_fsw(struct sc8581_device *cp, int *fsw)
{
	u8 byte, mask;
	int ret, val;

	ret = cp_read_byte(cp->client, SC8581_REG_0C, &byte);
	if (ret)
		return ret;
	mask = cp->chip_vendor == SC8585 ? SC8585_FSW_SET_MASK :
		SC8581_FSW_SET_MASK;
	val = (byte & mask) >> SC8581_FSW_SET_SHIFT;
	*fsw = cp->fsw_cfg.min + val * cp->fsw_cfg.step;
	return 0;
}

static int sc8581_get_tdie(struct sc8581_device *cp, int *tdie)
{
	u8 byte_h, byte_l;
	int ret, val = 0;

	if (!tdie)
		return -EINVAL;
	if (cp->chip_vendor != SC8585) {
		*tdie = 0;
		return 0;
	}
	ret = cp_read_byte(cp->client, SC8581_REG_27, &byte_h);
	ret |= cp_read_byte(cp->client, SC8581_REG_28, &byte_l);
	if (ret)
		return ret;
	val = ((byte_h & SC8581_TDIE_POL_H_MASK) << 8) |
	      (byte_l & SC8581_TDIE_POL_L_MASK);
	*tdie = val * SC8581_TDIE_ADC_LSB;
	return 0;
}

static int sc8581_set_acdrv_up(struct sc8581_device *bq, bool enable)
{
	u8 val = (enable ? SC8581_ACDRV_UP_ENABLE : SC8581_ACDRV_UP_DISABLE)
		 << SC8581_ACDRV_UP_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_7C,
			      SC8581_ACDRV_UP_MASK, val);
}

enum cp_reg_idx {
	VBAT_OVP_REG = 0,
	IBAT_OCP_REG,
	VUSB_OVP_REG,
	VWPC_OVP_REG,
	VOUT_VBUS_OVP_REG,
	IBUS_OCP_REG,
	IBUS_UCP_REG,
	PMID2OUT_OVP_REG,
	PMID2OUT_UVP_REG,
	CONVERTER_STATE_REG,
	CTRL1_REG,
	CTRL2_REG,
	CTRL3_REG,
	CTRL4_REG,
	CTRL5_REG,
	INT_STAT_REG,
	INT_FLAG_REG,
	FLT_FLAG_REG,
	DEVICE_ID_REG,
	FAULT_STATUS_REG,
	CP_REG_MAX,
};

static void sc8581_abnormal_charging_judge(struct sc8581_device *bq, u8 *data)
{
	int val[2] = { 0 };

	if (!data)
		return;
	if (!(data[INT_STAT_REG] & SC8581_VOUT_INSERT_STAT_MASK))
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_VOUT_UVLO, NULL);
	if (data[VBAT_OVP_REG] & SC8581_BAT_OVP_FLAG_MASK)
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_VBAT_OVP, NULL);
	if (bq->chip_vendor != SC8585 &&
	    (data[IBAT_OCP_REG] & SC8581_BAT_OCP_FLAG_MASK))
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_IBAT_OCP, NULL);
	if (data[VUSB_OVP_REG] & SC8581_USB_OVP_FLAG_MASK)
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_VUSB_OVP, NULL);
	if (data[VWPC_OVP_REG] & SC8581_WPC_OVP_FLAG_MASK)
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_VWPC_OVP, NULL);
	if (data[IBUS_OCP_REG] & SC8581_BUS_OCP_FLAG_MASK)
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_IBUS_OCP, NULL);
	if (data[IBUS_UCP_REG] & SC8581_BUS_UCP_FALL_FLAG_MASK)
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_IBUS_UCP, NULL);
	if (data[PMID2OUT_OVP_REG] & SC8581_PMID2OUT_OVP_FLAG_MASK)
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_PMID2OUT_OVP, NULL);
	if (data[PMID2OUT_UVP_REG] & SC8581_PMID2OUT_UVP_FLAG_MASK)
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_PMID2OUT_UVP, NULL);
	if (data[CONVERTER_STATE_REG] & SC8581_POR_FLAG_MASK)
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_POR_FLAG, NULL);
	if (data[FLT_FLAG_REG] & SC8581_TSHUT_FLAG_MASK) {
		cp_get_adc_data(bq, ADC_TDIE, (u32 *)&val[0]);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_TSHUT_FLAG, val);
	}
	if (data[FLT_FLAG_REG] & SC8581_VBUS_OVP_FLAG_MASK)
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_VBUS_OVP, NULL);
}

static int sc8581_dump_important_regs(struct sc8581_device *bq,
				      union cp_propval *unused)
{
	u8 data[CP_DUMP_REG_LEN] = { 0 };
	u8 reg[CP_REG_MAX] = { 0 };
	int ret;

	ret = cp_read_i2c_block_data(bq->client, SC8581_REG_00,
				     CP_DUMP_REG_LEN, data);
	if (ret < 0)
		return ret;
	memcpy(reg, data + 1, min_t(size_t, 15, sizeof(reg)));

	ret = cp_read_i2c_block_data(bq->client, SC8581_REG_10,
				     CP_DUMP_REG_LEN, data);
	if (ret < 0)
		return ret;
	reg[INT_STAT_REG] = data[0];
	reg[INT_FLAG_REG] = data[1];
	reg[FLT_FLAG_REG] = data[3];

	ret = cp_read_i2c_block_data(bq->client, SC8581_REG_6E, 4, data);
	if (ret < 0)
		return ret;
	reg[DEVICE_ID_REG] = data[0];
	reg[FAULT_STATUS_REG] = data[2];
	if (!(reg[CONVERTER_STATE_REG] & SC8581_CP_SWITCHING_STAT_MASK))
		sc8581_abnormal_charging_judge(bq, reg);
	return 0;
}

static int sc8581_init_int_src(struct sc8581_device *bq)
{
	int ret;

	ret = sc8581_set_batovp_alarm_int_mask(bq, SC8581_BAT_OVP_ALM_NOT_MASK);
	if (ret)
		return ret;
	return sc8581_set_busocp_alarm_int_mask(bq,
						SC8581_BUS_OCP_ALM_NOT_MASK);
}

static int sc8581_init_protection(struct sc8581_device *cp, int work_mode)
{
	int ret = 0;

	ret |= sc8581_enable_batovp(cp, true);
	ret |= sc8581_enable_batocp(cp, false);
	ret |= sc8581_enable_busocp(cp, true);
	ret |= sc8581_enable_busucp(cp, true);
	ret |= sc8581_enable_pmid2outovp(cp, true);
	ret |= sc8581_enable_pmid2outuvp(cp, true);
	ret |= sc8581_enable_batovp_alarm(cp, true);
	ret |= sc8581_enable_busocp_alarm(cp, true);
	ret |= sc8581_set_batovp_th(cp, cp->cfg.bat_ovp_th);
	ret |= sc8581_set_batocp_th(cp, BAT_OCP_TH);
	ret |= sc8581_set_batovp_alarm_th(cp, cp->cfg.bat_ovp_alarm_th);
	ret |= sc8581_set_busovp_th(cp, cp->cfg.bus_ovp_th[work_mode]);
	ret |= sc8581_set_usbovp_th(cp, cp->cfg.usb_ovp_th[work_mode]);
	ret |= sc8581_set_busocp_th(cp, cp->cfg.bus_ocp_th[work_mode]);
	ret |= sc8581_set_wpcovp_th(cp, cp->cfg.wpc_ovp_th);
	ret |= sc8581_set_outovp_th(cp, cp->cfg.out_ovp_th);
	ret |= sc8581_set_pmid2outuvp_th(cp, cp->cfg.pmid2out_uvp_th);
	ret |= sc8581_set_pmid2outovp_th(cp, cp->cfg.pmid2out_ovp_th);
	return ret;
}

static int sc8581_init_adc(struct sc8581_device *cp)
{
	int ch;

	sc8581_set_adc_scanrate(cp, false);
	for (ch = ADC_IBUS; ch < ADC_MAX_NUM; ch++) {
		if (cp->chip_vendor == SC8585 &&
		    (ch == ADC_IBAT || ch == ADC_TBAT))
			continue;
		sc8581_set_adc_scan(cp, ch, true);
	}
	return sc8581_enable_adc(cp, false);
}

static int sc8581_init_device(struct sc8581_device *cp)
{
	int ret, retry;

	for (retry = 0; retry < ERROR_RECOVERY_COUNT; retry++) {
		ret = sc8581_set_reg_reset(cp);
		ret |= sc8581_enable_parallel_func(cp, false);
		ret |= sc8581_enable_acdrv_manual_ovpgate_wpcgate(cp, true);
		ret |= sc8581_set_ss_timeout(cp, SC8581_SS_TIMEOUT_5120MS);
		ret |= sc8581_set_ucp_fall_dg(cp, SC8581_BUS_UCP_FALL_DG_5MS);
		if (cp->chip_vendor != SC8585) {
			ret |= sc8581_set_acdrv_up(cp, true);
			ret |= sc8581_set_sense_resistor(cp, 1);
			ret |= sc8581_set_enable_tsbat(cp, false);
		}
		ret |= sc8581_set_sync(cp, SC8581_SYNC_NO_SHIFT);
		ret |= sc8581_set_ovpgate_on_dg_set(cp, false);
		ret |= sc8581_init_adc(cp);
		ret |= sc8581_init_int_src(cp);
		ret |= sc8581_set_operation_mode(cp,
						 SC8581_FORWARD_2_1_CHARGER_MODE);
		if (cp->chip_vendor == SC8581) {
			ret |= sc8581_tsbat_flt_dis(cp, true);
			ret |= sc8581_pin_diag_dis(cp, true);
		}
		if (cp->chip_vendor == SC8585) {
			cp->fsw_cfg.min = SC8585_FSW_MIN;
			cp->fsw_cfg.max = SC8585_FSW_MAX;
			cp->fsw_cfg.step = SC8585_FSW_STEP;
		} else {
			cp->fsw_cfg.min = SC8581_FSW_MIN;
			cp->fsw_cfg.max = SC8581_FSW_MAX;
			cp->fsw_cfg.step = SC8581_FSW_STEP;
		}
		ret |= sc8581_set_fsw(cp, CP_DEFAULT_FSW);
		ret |= cp_write_byte(cp->client, SC8581_REG_42, 0x82);
		if (!ret)
			return 0;
		msleep(20);
	}
	return ret ?: -EIO;
}

static int cp_charge_detect_device(struct sc8581_device *bq)
{
	u8 data;
	int ret, retry;

	for (retry = 0; retry < ERROR_RECOVERY_COUNT; retry++) {
		ret = cp_read_byte(bq->client, SC8581_REG_6E, &data);
		if (!ret)
			break;
		msleep(100);
	}
	if (ret)
		return ret;
	if (data == SC8585_DEVICE_ID)
		bq->chip_vendor = SC8585;
	else if (data == SC8581_DEVICE_ID)
		bq->chip_vendor = SC8581;
	else if (data == SC8561_DEVICE_ID)
		bq->chip_vendor = SC8561;
	else
		return -ENODEV;
	mca_log_info("%s detected device id 0x%x model=%d\n",
		     bq->log_tag, data, bq->chip_vendor);
	return 0;
}

static int cp_get_adc_data(struct sc8581_device *bq, int channel, u32 *result)
{
	return sc8581_get_adc_data(bq, channel, result);
}

static int ops_cp_get_int_stat(int channel, bool *result, void *data)
{
	return sc8581_get_int_stat(data, channel, result);
}

static int ops_cp_dump_register(void *data)
{
	return sc8581_dump_important_regs(data, NULL);
}

static int ops_cp_get_chip_vendor(int *chip_id, void *data)
{
	*chip_id = ((struct sc8581_device *)data)->chip_vendor;
	return 0;
}

static int ops_cp_enable_charge(bool enable, void *data)
{
	return sc8581_enable_charge(data, enable);
}

static int ops_cp_get_charge_enable(bool *enabled, void *data)
{
	return sc8581_check_charge_enabled(data, enabled);
}

static int ops_cp_enable_qb(bool enable, void *data)
{
	return sc8581_enable_qb(data, enable);
}

static int ops_cp_set_rcp(bool enable, void *data)
{
	return sc8581_disable_rcp(data, enable);
}

static int ops_cp_set_pmid2outuvp_th(int value, void *data)
{
	return sc8581_set_pmid2outuvp_th(data, value);
}

static int ops_cp_get_vbus(u32 *val, void *data)
{
	return cp_get_adc_data(data, ADC_VBUS, val);
}

static int ops_cp_get_vusb(u32 *val, void *data)
{
	return cp_get_adc_data(data, ADC_VUSB, val);
}

static int ops_cp_get_ibus(u32 *val, void *data)
{
	return cp_get_adc_data(data, ADC_IBUS, val);
}

static int ops_cp_get_vbatt(u32 *val, void *data)
{
	return cp_get_adc_data(data, ADC_VBAT, val);
}

static int ops_cp_get_ibatt(u32 *val, void *data)
{
	return cp_get_adc_data(data, ADC_IBAT, val);
}

static int ops_cp_get_vout(u32 *val, void *data)
{
	return cp_get_adc_data(data, ADC_VOUT, val);
}

static int ops_cp_set_mode(int value, void *data)
{
	return sc8581_set_operation_mode(data, value);
}

static int ops_cp_get_mode(int *mode, void *data)
{
	return sc8581_get_operation_mode(data, mode);
}

static int ops_cp_device_init(int value, void *data)
{
	return sc8581_init_device(data);
}

static int ops_cp_enable_adc(bool enable, void *data)
{
	return sc8581_enable_adc(data, enable);
}

static int ops_cp_get_bypass_support(bool *enabled, void *data)
{
	*enabled = true;
	return 0;
}

static int ops_enable_acdrv_manual(bool enable, void *data)
{
	return sc8581_enable_acdrv_manual(data, enable);
}

static int ops_cp_enable_wpcgate(bool enable, void *data)
{
	return sc8581_enable_wpcgate(data, enable);
}

static int ops_cp_enable_ovpgate(bool enable, void *data)
{
	return sc8581_enable_ovpgate(data, enable);
}

static int ops_cp_enable_ovpgate_with_check(int type_temp, bool en, void *data)
{
	return sc8581_enable_ovpgate(data, en);
}

static int ops_cp_get_ovpgate_status(bool *enable, void *data)
{
	return sc8581_get_ovpgate_status(data, enable);
}

static int ops_cp_get_present(bool *present, void *data)
{
	*present = ((struct sc8581_device *)data)->chip_ok;
	return 0;
}

static int ops_cp_get_battery_temperature(u32 *val, void *data)
{
	return cp_get_adc_data(data, ADC_TBAT, val);
}

static int ops_cp_get_battery_present(bool *present, void *data)
{
	return sc8581_get_int_stat(data, VOUT_INSERT_STAT, present);
}

static int ops_cp_get_errorhl_stat(int *stat, void *data)
{
	struct sc8581_device *bq = data;
	u8 val;
	int ret = cp_read_byte(bq->client, SC8581_REG_0A, &val);

	if (ret)
		return ret;
	*stat = CP_PMID_ERROR_OK;
	if (val & SC8581_VBUS_ERRORLO_STAT_MASK)
		*stat = CP_PMID_ERROR_LOW;
	else if (val & SC8581_VBUS_ERRORHI_STAT_MASK)
		*stat = CP_PMID_ERROR_HIGH;
	return 0;
}

static int ops_cp_enable_busucp(bool en, void *data)
{
	return sc8581_enable_busucp(data, en);
}

static int ops_cp_set_fsw(int fsw, void *data)
{
	return sc8581_set_fsw(data, fsw);
}

static int ops_cp_set_default_fsw(void *data)
{
	return sc8581_set_fsw(data, CP_DEFAULT_FSW);
}

static int ops_cp_get_fsw(int *fsw, void *data)
{
	return sc8581_get_fsw(data, fsw);
}

static int ops_cp_get_fsw_step(int *step, void *data)
{
	*step = ((struct sc8581_device *)data)->fsw_cfg.step;
	return 0;
}

static int ops_cp_get_tdie(int *tdie, void *data)
{
	return sc8581_get_tdie(data, tdie);
}

static int ops_cp_set_adjustadble_timeout(int value, void *data)
{
	struct sc8581_device *bq = data;
	return cp_write_byte(bq->client, SC8581_REG_0D, value);
}

static int ops_cp_set_revchg(bool enable, void *data)
{
	struct sc8581_device *bq = data;
	int ret;

	if (enable) {
		ret = sc8581_set_operation_mode(bq,
						SC8581_REVERSE_1_2_CONVERTER_MODE);
		if (ret)
			return ret;
		return sc8581_enable_ovpgate(bq, true);
	}
	ret = sc8581_enable_charge(bq, false);
	ret |= sc8581_enable_qb(bq, false);
	ret |= sc8581_set_operation_mode(bq,
					 SC8581_FORWARD_2_1_CHARGER_MODE);
	return ret;
}

static struct platform_class_cp_ops sc8581_chg_ops = {
	.cp_set_enable = ops_cp_enable_charge,
	.cp_get_enabled = ops_cp_get_charge_enable,
	.cp_get_bus_voltage = ops_cp_get_vbus,
	.cp_get_bus_current = ops_cp_get_ibus,
	.cp_get_battery_voltage = ops_cp_get_vbatt,
	.cp_get_battery_current = ops_cp_get_ibatt,
	.cp_get_battery_temperature = ops_cp_get_battery_temperature,
	.cp_get_battery_present = ops_cp_get_battery_present,
	.cp_set_mode = ops_cp_set_mode,
	.cp_set_revchg = ops_cp_set_revchg,
	.cp_set_adjustadble_timeout = ops_cp_set_adjustadble_timeout,
	.cp_get_mode = ops_cp_get_mode,
	.cp_device_init = ops_cp_device_init,
	.cp_enable_adc = ops_cp_enable_adc,
	.cp_get_bypass_support = ops_cp_get_bypass_support,
	.cp_dump_register = ops_cp_dump_register,
	.cp_get_chip_vendor = ops_cp_get_chip_vendor,
	.cp_enable_acdrv_manual = ops_enable_acdrv_manual,
	.cp_enable_ovpgate = ops_cp_enable_ovpgate,
	.cp_enable_ovpgate_with_check = ops_cp_enable_ovpgate_with_check,
	.cp_enable_wpcgate = ops_cp_enable_wpcgate,
	.cp_get_ovpgate_status = ops_cp_get_ovpgate_status,
	.cp_get_present = ops_cp_get_present,
	.cp_get_usb_voltage = ops_cp_get_vusb,
	.cp_get_int_stat = ops_cp_get_int_stat,
	.cp_get_errorhl_stat = ops_cp_get_errorhl_stat,
	.cp_enable_busucp = ops_cp_enable_busucp,
	.cp_set_fsw = ops_cp_set_fsw,
	.cp_set_default_fsw = ops_cp_set_default_fsw,
	.cp_get_fsw = ops_cp_get_fsw,
	.cp_get_fsw_step = ops_cp_get_fsw_step,
	.cp_get_tdie = ops_cp_get_tdie,
	.cp_set_qb = ops_cp_enable_qb,
	.cp_set_pmid2outuvp_th = ops_cp_set_pmid2outuvp_th,
	.cp_set_rcp = ops_cp_set_rcp,
};

static void sc8581_irq_handler(struct work_struct *work)
{
	struct sc8581_device *bq = container_of(work, struct sc8581_device,
						irq_handle_work.work);
	bool usb_present = false;

	if (!bq->i2c_is_working)
		return;
	if (!sc8581_get_int_stat(bq, VUSB_PRESENT_STAT, &usb_present))
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
			usb_present ? MCA_EVENT_CP_VUSB_INSERT : MCA_EVENT_CP_VUSB_OUT,
			NULL);
	sc8581_dump_important_regs(bq, NULL);
}

static irqreturn_t sc8581_int_isr(int irq, void *private)
{
	struct sc8581_device *bq = private;

	pm_wakeup_dev_event(bq->dev, 500, true);
	schedule_delayed_work(&bq->irq_handle_work, 0);
	return IRQ_HANDLED;
}

static int sc8581_parse_dt(struct sc8581_device *bq)
{
	struct device_node *np = bq->dev->of_node;
	u32 role = 0;

	if (!np)
		return -EINVAL;
	mca_parse_dts_u32(np, "ic_role", &role, 0);
	bq->cp_role = role;
	strscpy(bq->log_tag, bq->cp_role == SC8581_SLAVE ? "[1]" : "[0]",
		sizeof(bq->log_tag));

	bq->irq_gpio = of_get_named_gpio(np, "cp-int", 0);
	if (!gpio_is_valid(bq->irq_gpio))
		return -EINVAL;
	bq->nlpm_gpio = of_get_named_gpio(np, "cp-nlpm-gpio", 0);
	if (!gpio_is_valid(bq->nlpm_gpio))
		return -EINVAL;

	mca_parse_dts_u32(np, "bat-ovp-threshold", &bq->cfg.bat_ovp_th,
			  BAT_OVP_TH);
	mca_parse_dts_u32(np, "bat-ovp-alarm-threshold",
			  &bq->cfg.bat_ovp_alarm_th, BAT_OVP_ALARM_TH);
	mca_parse_dts_u32_array(np, "bus-ovp-threshold", bq->cfg.bus_ovp_th,
				CP_MODE_DIV_MAX);
	mca_parse_dts_u32_array(np, "usb-ovp-threshold", bq->cfg.usb_ovp_th,
				CP_MODE_DIV_MAX);
	mca_parse_dts_u32_array(np, "bus-ocp-threshold", bq->cfg.bus_ocp_th,
				CP_MODE_DIV_MAX);
	mca_parse_dts_u32(np, "wpc-ovp-threshold", &bq->cfg.wpc_ovp_th,
			  WPC_OVP_TH);
	mca_parse_dts_u32(np, "out-ovp-threshold", &bq->cfg.out_ovp_th,
			  OUT_OVP_TH);
	mca_parse_dts_u32(np, "pmid2-uvp-threshold", &bq->cfg.pmid2out_uvp_th,
			  PMID2OUT_UVP_TH);
	mca_parse_dts_u32(np, "pmid2-ovp-threshold", &bq->cfg.pmid2out_ovp_th,
			  PMID2OUT_OVP_TH);
	return 0;
}

static int sc8581_register_irq(struct sc8581_device *bq)
{
	int ret;

	ret = devm_gpio_request(bq->dev, bq->irq_gpio, dev_name(bq->dev));
	if (ret)
		return ret;
	bq->irq = gpio_to_irq(bq->irq_gpio);
	if (bq->irq < 0)
		return bq->irq;
	ret = devm_request_threaded_irq(bq->dev, bq->irq, NULL, sc8581_int_isr,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					dev_name(bq->dev), bq);
	if (ret)
		return ret;
	return enable_irq_wake(bq->irq);
}

static int sc8581_init_gpio(struct sc8581_device *bq)
{
	int ret;

	if (bq->cp_role != SC8581_MASTER)
		return 0;
	ret = devm_gpio_request(bq->dev, bq->nlpm_gpio, dev_name(bq->dev));
	if (ret)
		return ret;
	ret = gpio_direction_output(bq->nlpm_gpio, 1);
	if (!ret)
		msleep(400);
	return ret;
}

static int sc8581_dump_log_head(void *data, char *buf, int size)
{
	struct sc8581_device *bq = data;

	if (!bq)
		return 0;
	return snprintf(buf, size, bq->cp_role == 0 ?
		"cp_vusb cp_vbus cp_ibus cp_ibat cp_vbat cp_vout " :
		"cp_vusb1 cp_vbus1 cp_ibus1 cp_ibat1 cp_vbat1 cp_vout1 ");
}

static int sc8581_dump_log_context(void *data, char *buf, int size)
{
	struct sc8581_device *bq = data;
	u32 vbus = 0, vusb = 0, ibus = 0, ibat = 0, vbat = 0, vout = 0;

	if (!bq)
		return 0;
	ops_cp_get_vbus(&vbus, data);
	ops_cp_get_vusb(&vusb, data);
	ops_cp_get_ibus(&ibus, data);
	ops_cp_get_ibatt(&ibat, data);
	ops_cp_get_vbatt(&vbat, data);
	ops_cp_get_vout(&vout, data);
	return snprintf(buf, size, "%-8u%-8u%-8u%-8u%-8u%-8u",
			vusb, vbus, ibus, ibat, vbat, vout);
}

static struct mca_log_charge_log_ops sc8581_log_ops = {
	.dump_log_head = sc8581_dump_log_head,
	.dump_log_context = sc8581_dump_log_context,
};

static bool mca_cp_is_en_buck_project_hwid(void)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();

	return hwid && hwid->platform_version == EN_BUCK_PROJECT_PLATFORM_VER &&
	       hwid->hwid_value == EN_BUCK_PROJECT_HWID;
}

static int sc8581_probe(struct i2c_client *client)
{
	struct sc8581_device *bq;
	int ret;

	bq = devm_kzalloc(&client->dev, sizeof(*bq), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;
	bq->client = client;
	bq->dev = &client->dev;
	i2c_set_clientdata(client, bq);

	if (mca_cp_is_en_buck_project_hwid())
		client->addr = SC8581_EN_BUCK_CP_I2C_ADDR;
	ret = sc8581_parse_dt(bq);
	if (ret)
		return ret;
	ret = sc8581_init_gpio(bq);
	if (ret)
		return ret;
	ret = cp_charge_detect_device(bq);
	if (ret) {
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO,
				       MCA_EVENT_CP_IIC_ERROR, NULL);
		return ret;
	}

	INIT_DELAYED_WORK(&bq->irq_handle_work, sc8581_irq_handler);
	ret = sc8581_register_irq(bq);
	if (ret)
		return ret;
	ret = sc8581_init_device(bq);
	if (ret)
		return ret;

	bq->chip_ok = true;
	bq->ovpgate_en = true;
	bq->i2c_is_working = true;
#ifdef CONFIG_DEBUG_FS
	reg_info.address = SC8581_REG_00;
	reg_info.count = 1;
	mca_debugfs_create_group(bq->cp_role == SC8581_SLAVE ?
				 "sc85xx_01" : "sc85xx_00",
				 cp_debugfs_field_tbl, CP_DEBUGFS_ATTRS_SIZE, bq);
#endif
	ret = platform_class_cp_register_ops(bq->cp_role, &sc8581_chg_ops, bq);
	if (ret)
		return ret;
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_CP_MASTER_IC,
				    &sc8581_log_ops, bq);
	device_init_wakeup(bq->dev, true);
	schedule_delayed_work(&bq->irq_handle_work, 0);
	mca_log_info("%s probe success, model=%d addr=0x%02x\n",
		     bq->log_tag, bq->chip_vendor, client->addr);
	return 0;
}

static int sc8581_suspend(struct device *dev)
{
	struct sc8581_device *bq = i2c_get_clientdata(to_i2c_client(dev));
	int ret = sc8581_enable_adc(bq, false);

	bq->i2c_is_working = false;
	return ret;
}

static int sc8581_resume(struct device *dev)
{
	return 0;
}

static void sc8581_i2c_complete(struct device *dev)
{
	struct sc8581_device *bq = i2c_get_clientdata(to_i2c_client(dev));

	if (bq)
		bq->i2c_is_working = true;
}

static const struct dev_pm_ops sc8581_pm_ops = {
	.suspend = sc8581_suspend,
	.resume = sc8581_resume,
	.complete = sc8581_i2c_complete,
};

static void sc8581_remove(struct i2c_client *client)
{
	struct sc8581_device *bq = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&bq->irq_handle_work);
	sc8581_enable_adc(bq, false);
	bq->i2c_is_working = false;
}

static void sc8581_shutdown(struct i2c_client *client)
{
	struct sc8581_device *bq = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&bq->irq_handle_work);
	sc8581_enable_adc(bq, false);
	if (gpio_is_valid(bq->nlpm_gpio))
		gpio_direction_output(bq->nlpm_gpio, 0);
}

static const struct of_device_id sc8581_of_match[] = {
	{ .compatible = "sc8581" },
	{ .compatible = "sc8585_master" },
	{ .compatible = "sc8585_slave" },
	{},
};
MODULE_DEVICE_TABLE(of, sc8581_of_match);

static struct i2c_driver sc8581_driver = {
	.driver = {
		.name = "sc8581_charger_pump",
		.of_match_table = sc8581_of_match,
		.pm = &sc8581_pm_ops,
	},
	.probe = sc8581_probe,
	.remove = sc8581_remove,
	.shutdown = sc8581_shutdown,
};
module_i2c_driver(sc8581_driver);

MODULE_AUTHOR("Xiaomi Technologies / Dada MCA reconstruction");
MODULE_DESCRIPTION("Southchip SC8581/SC8585 MCA charge-pump driver");
MODULE_LICENSE("GPL v2");
