// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2024, Texas Instruments Incorporated - https://www.ti.com/
 * Copyright (C) 2025, Variscite Ltd. - https://www.variscite.com/
 */

#include <fdt_support.h>
#include <dm/uclass.h>
#include <k3-ddrss.h>
#include <spl.h>
#include <mach/k3-ddr.h>

#include "k3-ddr.h"
#include "am62x_eeprom.h"
#include "am62x_dram.h"

int dram_init(void)
{
	int ret;

	read_eeprom_header();

	ret = fdtdec_setup_mem_size_base_lowest();
	if (ret) {
		printf("Error setting up mem size and base. %d\n", ret);
		return ret;
	}
	else
		/* Override fdtdec_setup_mem_size_base_lowest with memory size from EEPROM */
		ret = var_dram_init_mem_size_base();

	ret = k3_mem_map_init();
	if (ret)
		printf("Error setting up MMU table. %d\n", ret);

	return ret;
}

int dram_init_banksize(void)
{
	return var_dram_init_banksize();
}
