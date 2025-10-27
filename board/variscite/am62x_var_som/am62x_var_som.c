// SPDX-License-Identifier: GPL-2.0+
/*
 * Board specific initialization for AM62x platforms
 *
 * Copyright (C) 2020-2022 Texas Instruments Incorporated - https://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 * Copyright (C) 2023-2025 Variscite Ltd. - https://www.variscite.com/
 *
 */

#include <efi_loader.h>
#include <env.h>
#include <spl.h>
#include <init.h>
#include <video.h>
#include <splash.h>
#include <cpu_func.h>
#include <k3-ddrss.h>
#include <fdt_support.h>
#include <fdt_simplefb.h>
#include <asm/io.h>
#include <asm/arch/hardware.h>
#include <dm/uclass.h>
#include <asm/arch/k3-ddr.h>

#include "../common/am62x_eeprom.h"
#include "../common/am62x_dram.h"
#include "../common/am62x_eth.h"
#ifdef CONFIG_BOARD_LATE_INIT
#include "../common/am62x_mmc.h"
#endif

#include <asm/arch/k3-ddr.h>

#define RESERVED_MEMORY SZ_64M

int var_setup_mac(struct var_eeprom *eeprom);

DECLARE_GLOBAL_DATA_PTR;

struct efi_fw_image fw_images[] = {
	{
		.image_type_id = AM62X_VAR_SOM_TIBOOT3_IMAGE_GUID,
		.fw_name = u"AM62X_VAR_SOM_TIBOOT3",
		.image_index = 1,
	},
	{
		.image_type_id = AM62X_VAR_SOM_SPL_IMAGE_GUID,
		.fw_name = u"AM62X_VAR_SOM_SPL",
		.image_index = 2,
	},
	{
		.image_type_id = AM62X_VAR_SOM_UBOOT_IMAGE_GUID,
		.fw_name = u"AM62X_VAR_SOM_UBOOT",
		.image_index = 3,
	}
};

struct efi_capsule_update_info update_info = {
	.dfu_string = "sf 0:0=tiboot3.bin raw 0 80000;"
	"tispl.bin raw 80000 200000;u-boot.img raw 280000 400000",
	.num_images = ARRAY_SIZE(fw_images),
	.images = fw_images,
};

#if IS_ENABLED(CONFIG_SET_DFU_ALT_INFO)
void set_dfu_alt_info(char *interface, char *devstr)
{
	if (IS_ENABLED(CONFIG_EFI_HAVE_CAPSULE_SUPPORT))
		env_set("dfu_alt_info", update_info.dfu_string);
}
#endif

int board_init(void)
{
	return 0;
}

phys_size_t get_effective_memsize(void)
{
	phys_size_t ram_size;;

	/*
	 * Just below 512MB are TF-A and OPTEE reserve regions, thus
	 * SPL/U-Boot RAM has to start below that. Leave 64MB space for
	 * all reserved memories.
	 */
	if (gd->ram_size == SZ_512M)
		ram_size = SZ_512M - RESERVED_MEMORY;
	else
		ram_size = gd->ram_size;

#ifndef CFG_MAX_MEM_MAPPED
	return ram_size;
#else
	/* limit stack to what we can reasonable map */
	return ((ram_size > CFG_MAX_MEM_MAPPED) ?
		CFG_MAX_MEM_MAPPED : ram_size);
#endif
}

#if defined(CONFIG_SPL_LOAD_FIT)
int board_fit_config_name_match(const char *name)
{
	return 0;
}
#endif

#if defined(CONFIG_XPL_BUILD)
void spl_board_init(void)
{
	u32 val;

#ifndef CONFIG_CPU_V7R
	/* Save boot_device for U-Boot */
	int * boot_device = (int *) VAR_SCRATCH_BOOT_DEVICE;
	*boot_device = spl_boot_device();
#endif

	/* We have 32k crystal, so lets enable it */
	val = readl(MCU_CTRL_LFXOSC_CTRL);
	val &= ~(MCU_CTRL_LFXOSC_32K_DISABLE_VAL);
	writel(val, MCU_CTRL_LFXOSC_CTRL);
	/* Add any TRIM needed for the crystal here.. */
	/* Make sure to mux up to take the SoC 32k from the crystal */
	writel(MCU_CTRL_DEVICE_CLKOUT_LFOSC_SELECT_VAL,
	       MCU_CTRL_DEVICE_CLKOUT_32K_CTRL);

	enable_caches();
	if (IS_ENABLED(CONFIG_SPL_SPLASH_SCREEN) && IS_ENABLED(CONFIG_SPL_BMP))
		splash_display();

	/* Init DRAM size for R5/A53 SPL */
	dram_init_banksize();
}

void spl_perform_fixups(struct spl_image_info *spl_image)
{
	if (IS_ENABLED(CONFIG_K3_DDRSS)) {
		if (IS_ENABLED(CONFIG_K3_INLINE_ECC))
			fixup_ddr_driver_for_ecc(spl_image);
	} else {
		fixup_memory_node(spl_image);
	}
}
#endif

#define ENV_STR_SIZE 10

#ifdef CONFIG_BOARD_LATE_INIT
void set_bootdevice_env(void) {
	int * boot_device = (int *) VAR_SCRATCH_BOOT_DEVICE;
	char env_str[ENV_STR_SIZE];

	snprintf(env_str, ENV_STR_SIZE, "%d", *boot_device);
	env_set("boot_dev", env_str);

	switch(*boot_device) {
	case BOOT_DEVICE_MMC2:
		printf("Boot Device: SD\n");
		env_set("boot_dev_name", "sd");
		break;
	case BOOT_DEVICE_MMC1:
		printf("Boot Device: eMMC\n");
		env_set("boot_dev_name", "emmc");
		break;
	default:
		printf("Boot Device: Unknown\n");
		env_set("boot_dev_name", "unknown");
		break;
	}
}

#define SDRAM_SIZE_STR_LEN 5

/* configure AUDIO_EXT_REFCLK1 pin as an output*/
static void audio_refclk1_ctrl_clkout_en(void) {
	volatile uint32_t *audio_refclk1_ctrl_ptr = (volatile uint32_t *)0x001082E4;
	uint32_t audio_refclk1_ctrl_val = *audio_refclk1_ctrl_ptr;
	audio_refclk1_ctrl_val |= (1 << 15);
	*audio_refclk1_ctrl_ptr = audio_refclk1_ctrl_val;
}

int board_late_init(void)
{
	struct var_eeprom *ep = VAR_EEPROM_DATA;
	char sdram_size_str[SDRAM_SIZE_STR_LEN];

	audio_refclk1_ctrl_clkout_en();

	env_set("board_name", "VAR-SOM-AM62");

	read_eeprom_header();
	var_eeprom_print_prod_info(ep);

	set_bootdevice_env();

	snprintf(sdram_size_str, SDRAM_SIZE_STR_LEN, "%d",
			(int) (gd->ram_size / 1024 / 1024));
	env_set("sdram_size", sdram_size_str);

#ifdef CONFIG_ENV_IS_IN_MMC
	board_late_mmc_env_init();
#endif

#ifdef CONFIG_TI_AM65_CPSW_NUSS
	var_setup_mac(ep);
	var_eth_get_rgmii_id_quirk(ep);
#endif

	return 0;
}
#endif

#define CTRLMMR_USB0_PHY_CTRL	0x43004008
#define CTRLMMR_USB1_PHY_CTRL	0x43004018
#define CORE_VOLTAGE		0x80000000

#if defined(CONFIG_OF_BOARD_SETUP)
int ft_board_setup(void *blob, struct bd_info *bd)
{
	int ret = -1;

	if (IS_ENABLED(CONFIG_FDT_SIMPLEFB))
		ret = fdt_simplefb_enable_and_mem_rsv(blob);

	/* If simplefb is not enabled and video is active, then at least reserve
	 * the framebuffer region to preserve the splash screen while OS is booting
	 */
	if (IS_ENABLED(CONFIG_VIDEO) && IS_ENABLED(CONFIG_OF_LIBFDT)) {
		if (ret && video_is_active())
			return fdt_add_fb_mem_rsv(blob);
	}

	return 0;
}
#endif
