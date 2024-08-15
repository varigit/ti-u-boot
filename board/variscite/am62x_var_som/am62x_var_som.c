// SPDX-License-Identifier: GPL-2.0+
/*
 * Board specific initialization for AM62x platforms
 *
 * Copyright (C) 2020-2022 Texas Instruments Incorporated - https://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 * Copyright (C) 2023-2024 Variscite Ltd. - https://www.variscite.com/
 *
 */

#include <common.h>
#include <asm/io.h>
#include <spl.h>
#include <video.h>
#include <splash.h>
#include <dm/uclass.h>
#include <k3-ddrss.h>
#include <fdt_simplefb.h>
#include <fdt_support.h>
#include <asm/arch/hardware.h>
#include <asm/arch/sys_proto.h>
#include <env.h>
#include <linux/sizes.h>

#include "../common/am62x_eeprom.h"
#include "../common/am62x_dram.h"
#include "../common/rtc.h"
#ifdef CONFIG_BOARD_LATE_INIT
#include "../common/am62x_mmc.h"
#endif

#include "../common/k3-ddr-init.h"

int var_setup_mac(struct var_eeprom *eeprom);

DECLARE_GLOBAL_DATA_PTR;

int board_init(void)
{
	if (IS_ENABLED(CONFIG_BOARD_HAS_32K_RTC_CRYSTAL))
		board_rtc_init();

	return 0;
}

int read_eeprom_header(void) {
	struct var_eeprom *ep = VAR_EEPROM_DATA;
	struct var_eeprom eeprom = {0};
	int ret = 0;

	if (!var_eeprom_is_valid(ep)) {
		ret = var_eeprom_read_header(&eeprom);
		if (ret) {
			printf("%s EEPROM read failed.\n", __func__);
			return -1;
		}
		memcpy(ep, &eeprom, sizeof(*ep));
	}

	return ret;
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
		ram_size = SZ_512M - SZ_64M;
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

#if defined(CONFIG_SPL_BUILD)
void spl_perform_fixups(struct spl_image_info *spl_image)
{
	if (IS_ENABLED(CONFIG_K3_INLINE_ECC))
		fixup_ddr_driver_for_ecc(spl_image);
	else
		fixup_memory_node(spl_image);
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

#ifdef CONFIG_SPL_BOARD_INIT
static int video_setup(void)
{
	if (CONFIG_IS_ENABLED(VIDEO)) {
		ulong addr;
		int ret;

		addr = gd->relocaddr;
		ret = video_reserve(&addr);
		if (ret)
			return ret;
		debug("Reserving %luk for video at: %08lx\n",
		      ((unsigned long)gd->relocaddr - addr) >> 10, addr);
		gd->relocaddr = addr;
	}

	return 0;
}

#if defined(CONFIG_OF_BOARD_SETUP)
int ft_board_setup(void *blob, struct bd_info *bd)
{
	int ret = -1;

	if (IS_ENABLED(CONFIG_VIDEO)) {
		if (IS_ENABLED(CONFIG_FDT_SIMPLEFB))
			ret = fdt_simplefb_enable_and_mem_rsv(blob);

		/* If simplefb is not enabled and video is active, then at least reserve
		 * the framebuffer region to preserve the splash screen while OS is booting
		 */
		if (ret && video_is_active())
			fdt_add_fb_mem_rsv(blob);
	}

	return 0;
}
#endif

void spl_board_init(void)
{
	u32 val;

#ifndef CONFIG_CPU_V7R
	/* Save boot_device for U-Boot */
	int * boot_device = (int *) VAR_SCRATCH_BOOT_DEVICE;
	*boot_device = spl_boot_device();
#endif

	/* Set USB0 PHY core voltage to 0.85V */
	val = readl(CTRLMMR_USB0_PHY_CTRL);
	val &= ~(CORE_VOLTAGE);
	writel(val, CTRLMMR_USB0_PHY_CTRL);

	/* Set USB1 PHY core voltage to 0.85V */
	val = readl(CTRLMMR_USB1_PHY_CTRL);
	val &= ~(CORE_VOLTAGE);
	writel(val, CTRLMMR_USB1_PHY_CTRL);

	video_setup();
	enable_caches();
	if (IS_ENABLED(CONFIG_SPL_SPLASH_SCREEN) && IS_ENABLED(CONFIG_SPL_BMP))
		splash_display();

	/* Init DRAM size for R5/A53 SPL */
	dram_init_banksize();
}
#endif
