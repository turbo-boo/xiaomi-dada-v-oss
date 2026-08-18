# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(srctree)/drivers/power/supply/mca/include

obj-$(CONFIG_XM_POWER_SUPPLY) += mca_basic_wireless.o
mca_basic_wireless-objs := mca_basic_wireless_core.o mca_wireless_class_compat.o

obj-$(CONFIG_XM_POWER_SUPPLY) += mca_quick_wireless.o
mca_quick_wireless-objs := mca_quick_wireless_baa.o

obj-$(CONFIG_XM_POWER_SUPPLY) += mca_wireless_revchg.o
mca_wireless_revchg-objs := mca_wireless_revchg_safe.o
