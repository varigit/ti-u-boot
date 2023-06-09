/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/
 *	Andreas Dannenberg <dannenberg@ti.com>
 */

#ifndef _SYS_PROTO_H_
#define _SYS_PROTO_H_

enum k3_device_type {
	K3_DEVICE_TYPE_BAD,
	K3_DEVICE_TYPE_GP,
	K3_DEVICE_TYPE_TEST,
	K3_DEVICE_TYPE_EMU,
	K3_DEVICE_TYPE_HS_FS,
	K3_DEVICE_TYPE_HS_SE,
};

enum k3_device_type get_device_type(void);

void sdelay(unsigned long loops);
u32 wait_on_value(u32 read_bit_mask, u32 match_value, void *read_addr,
		  u32 bound);
struct ti_sci_handle *get_ti_sci_handle(void);
int fdt_fixup_msmc_ram(void *blob, char *parent_path, char *node_name);
int do_board_detect(void);
void release_resources_for_core_shutdown(void);
int fdt_disable_node(void *blob, char *node_path);

bool soc_is_j721e(void);
bool soc_is_j7200(void);

void k3_spl_init(void);
void k3_mem_init(void);
bool check_rom_loaded_sysfw(void);
#endif
