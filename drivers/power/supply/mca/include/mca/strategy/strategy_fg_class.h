#ifndef _MCA_STRATEGY_STRATEGY_FG_CLASS_H_
#define _MCA_STRATEGY_STRATEGY_FG_CLASS_H_

#include <linux/types.h>

struct strategy_fg_class_ops {
	int (*strategy_fg_is_init_ok)(void *data);
	int (*strategy_fg_get_rsoc)(void *data, int *rsoc);
	int (*strategy_fg_get_soc)(void *data);
	int (*strategy_fg_get_temp)(void *data, int *temp);
	int (*strategy_fg_get_current)(void *data, int *curr);
	int (*strategy_fg_get_voltage)(void *data, int *volt);
	int (*strategy_fg_get_cycle)(void *data, int *cycle);
	int (*strategy_fg_get_voltage_mean)(void *data, int *vol_mean);
	int (*strategy_fg_get_soc_decimal_info)(void *data, int *decimal,
						int *rate);
	bool (*strategy_fg_get_charging_done)(void *data);
	int (*strategy_fg_set_charging_done)(void *data, bool done);
	int (*strategy_fg_get_model_name)(void *data, const char **name);
	int (*strategy_fg_set_fastcharge)(void *data, bool en);
	int (*strategy_fg_get_fastcharge)(void *data);
	int (*strategy_fg_get_authentic)(void *data, bool *authentic);
	int (*strategy_fg_get_dc)(void *data, int *dc);
	int (*strategy_fg_get_rm)(void *data, int *rm);
	int (*strategy_fg_get_fcc)(void *data, int *fcc);
	int (*strategy_fg_is_chip_ok)(void *data);
	int (*strategy_fg_get_health)(void *data, int *health);
	int (*strategy_fg_get_first_termination)(void *data, int *value);
	int (*strategy_fg_get_pack_vendor_id)(void *data, int *vendor_id);
	int (*strategy_fg_dual_is_chip_ok)(void *data, int index);
	int (*strategy_fg_get_thermal_temperature)(void *data, int *temp);
	int (*strategy_fg_get_soh)(void *data, int *soh);
	int (*strategy_fg_get_temp_offset_flag)(void *data, int *flag);
};

int strategy_class_fg_ops_register(void *data,
				   struct strategy_fg_class_ops *ops);
int strategy_class_fg_ops_is_init_ok(void);
int strategy_class_fg_ops_get_rsoc(int *rsoc);
int strategy_class_fg_ops_get_soc(void);
int strategy_class_fg_ops_get_temperature(int *temp);
int strategy_class_fg_ops_get_current(int *curr);
int strategy_class_fg_ops_get_voltage(int *volt);
int strategy_class_fg_ops_get_cyclecount(int *cycle);
int strategy_class_fg_get_voltage_mean(int *vol_mean);
int strategy_class_fg_ops_get_soc_decimal(int *soc_decimal, int *rate);
bool strategy_class_fg_ops_get_charging_done(void);
int strategy_class_fg_ops_set_charging_done(bool charging_done);
int strategy_class_fg_get_model_name(const char **model_name);
int strategy_class_fg_set_fastcharge(bool en);
int strategy_class_fg_get_fastcharge(void);
int strategy_class_fg_get_authentic(bool *authentic);
int strategy_class_fg_get_dc(int *dc);
int strategy_class_fg_get_rm(int *rm);
int strategy_class_fg_get_fcc(int *fcc);
int strategy_class_fg_is_chip_ok(void);
int strategy_class_fg_get_health(int *health);
int strategy_class_fg_get_first_termination(int *value);
int strategy_class_fg_get_pack_vendor_id(int *vendor_id);
int strategy_class_fg_dual_is_chip_ok(int index);
int strategy_class_fg_ops_get_thermal_temperature(int *temp);
int strategy_class_fg_get_soh(int *soh);
int strategy_class_fg_get_temp_offset_flag(int *flag);

#endif
