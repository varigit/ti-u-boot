#
# Copyright (C) 2020-2022 Texas Instruments Incorporated - https://www.ti.com/
#	Suman Anna <s-anna@ti.com>
# Copyright (C) 2023-2024 Variscite Ltd. - https://www.variscite.com/
#
# SPDX-License-Identifier:     GPL-2.0+
#

obj-y	+= am62x_var_som.o
obj-y += ../common/am62x_eeprom.o
obj-y += ../common/am62x_dram.o
ifndef CONFIG_CPU_V7R
obj-y += ../common/am62x_mmc.o
endif
obj-$(CONFIG_VIDEO) += ../common/video.o
obj-$(CONFIG_TI_AM65_CPSW_NUSS) += ../common/am62x_eth.o
obj-${CONFIG_ARCH_K3} += ../common/k3-ddr-init.o
obj-${CONFIG_BOARD_HAS_32K_RTC_CRYSTAL} += ../common/rtc.o
