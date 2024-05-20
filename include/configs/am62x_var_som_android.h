/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration header file for K3 AM625 SoC family: Android
 *
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 */

#include "am62x_evm_android.h"

#undef PREPARE_FDT_CMD
#undef EXTRA_ENV_ANDROID_ARGS
#undef CFG_EXTRA_ENV_SETTINGS

/*
 * Prepares complete device tree blob for current board (for Android boot).
 *
 * Boot image or recovery image should be loaded into $loadaddr prior to running
 * these commands. The logic of these commnads is next:
 *
 *   1. Read correct DTB for current SoC/board from boot image in $loadaddr
 *      to $fdtaddr
 *   2. Merge all needed DTBO for current board from 'dtbo' partition into read
 *      DTB
 *   3. User should provide $fdtaddr as 3rd argument to 'bootm'
 */
#define PREPARE_FDT_CMD "prepare_fdt_cmd=" \
	"echo Preparing FDT...; " \
	"if test $board_name = VAR-SOM-AM62; then " \
		"echo \"  Reading DTB for VAR-SOM-AM62...\"; " \
		"setenv dtb_index 0;" \
	"else " \
		"echo Error: Android boot is not supported for $board_name; " \
		"exit; " \
	"fi; " \
	"abootimg get dtb --index=$dtb_index dtb_start dtb_size; " \
	"cp.b $dtb_start $fdt_addr_r $dtb_size; " \
	"fdt addr $fdt_addr_r $fdt_size; " \
	"part start mmc ${mmcdev} dtbo${slot_suffix} dtbo_start; " \
	"part size mmc ${mmcdev} dtbo${slot_suffix} dtbo_size; " \
	"mmc read ${dtboaddr} ${dtbo_start} ${dtbo_size}; " \
	"echo \"  Applying DTBOs...\"; " \
	"adtimg addr $dtboaddr; " \
	"dtbo_idx=''; " \
	"for index in $dtbo_index; do " \
		"adtimg get dt --index=$index dtbo_addr; " \
		"fdt resize 4096; " \
		"fdt apply $dtbo_addr; " \
		"if test $dtbo_idx = ''; then " \
			"dtbo_idx=${index}; " \
		"else " \
			"dtbo_idx=${dtbo_idx},${index}; " \
		"fi; " \
	"done; " \
	"setenv bootargs \"$bootargs androidboot.dtbo_idx=$dtbo_idx \"; \0"

#define EXTRA_ENV_ANDROID_ARGS \
	AB_SELECT_SLOT_CMD \
	AVB_VERIFY_CMD \
	PREPARE_FDT_CMD \
	BOOTENV

#define CFG_EXTRA_ENV_SETTINGS \
	BOOTENV \
	EXTRA_ENV_ANDROID_ARGS
