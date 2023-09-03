// SPDX-License-Identifier: GPL-2.0+
/*
 * board.c
 *
 * Board functions for Variscite VAR-SOM-AM33
 *
 * Copyright (C) 2023, Variscite Ltd - http://www.variscite.com/
 */

#include <common.h>
#include <dm.h>
#include <env.h>
#include <errno.h>
#include <image.h>
#include <init.h>
#include <hang.h>
#include <malloc.h>
#include <net.h>
#include <spl.h>
#include <serial.h>
#include <asm/arch/cpu.h>
#include <asm/arch/hardware.h>
#include <asm/arch/omap.h>
#include <asm/arch/ddr_defs.h>
#include <asm/arch/clock.h>
#include <asm/arch/gpio.h>
#include <asm/arch/mmc_host_def.h>
#include <asm/arch/sys_proto.h>
#include <asm/arch/mem.h>
#include <asm/io.h>
#include <asm/emif.h>
#include <asm/gpio.h>
#include <asm/omap_common.h>
#include <asm/omap_sec_common.h>
#include <asm/omap_mmc.h>
#include <i2c.h>
#include <miiphy.h>
#include <cpsw.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <power/tps65910.h>
#include <env_internal.h>
#include <watchdog.h>
#include "board.h"

DECLARE_GLOBAL_DATA_PTR;

#define GPIO_LCD_BACKLIGHT     2
#define GPIO_BT_UART_SELECT    20
#define GPIO_SOM_REV_BIT0_GPIO 77
#define GPIO_SOM_REV_BIT1_GPIO 86
#define GPIO_SOM_REV_BIT2_GPIO 75
#define GPIO_PHY1_RST          83
#define GPIO_PHY2_RST          106

static struct ctrl_dev *cdev = (struct ctrl_dev *)CTRL_DEVICE_BASE;

#ifndef CONFIG_DM_SERIAL
struct serial_device *default_serial_console(void)
{
	return &eserial1_device;
}
#endif

#ifndef CONFIG_SKIP_LOWLEVEL_INIT
static const struct ddr_data ddr3_am335x_var_som_data = {
	.datardsratio0 = MT41K256M16HA125E_RD_DQS,
	.datawdsratio0 = MT41K256M16HA125E_WR_DQS,
	.datafwsratio0 = MT41K256M16HA125E_PHY_FIFO_WE,
	.datawrsratio0 = MT41K256M16HA125E_PHY_WR_DATA,
};

static const struct cmd_control ddr3_am335x_var_som_cmd_ctrl_data = {
	.cmd0csratio = MT41K256M16HA125E_RATIO,
	.cmd0iclkout = MT41K256M16HA125E_INVERT_CLKOUT,

	.cmd1csratio = MT41K256M16HA125E_RATIO,
	.cmd1iclkout = MT41K256M16HA125E_INVERT_CLKOUT,

	.cmd2csratio = MT41K256M16HA125E_RATIO,
	.cmd2iclkout = MT41K256M16HA125E_INVERT_CLKOUT,
};

static struct emif_regs ddr3_am335x_var_som_emif_reg_data = {
	.sdram_config = MT41K256M16HA125E_EMIF_SDCFG,
	.ref_ctrl = MT41K256M16HA125E_EMIF_SDREF,
	.sdram_tim1 = MT41K256M16HA125E_EMIF_TIM1,
	.sdram_tim2 = MT41K256M16HA125E_EMIF_TIM2,
	.sdram_tim3 = MT41K256M16HA125E_EMIF_TIM3,
	.ocp_config = EMIF_OCP_CONFIG_AM335X_VAR_SOM,
	.zq_config = MT41K256M16HA125E_ZQ_CFG,
	.emif_ddr_phy_ctlr_1 = MT41K256M16HA125E_EMIF_READ_LATENCY,
};

#ifdef CONFIG_SPL_OS_BOOT
int spl_start_uboot(void)
{
#ifdef CONFIG_SPL_SERIAL_SUPPORT
	/* break into full u-boot on 'c' */
	if (serial_tstc() && serial_getc() == 'c')
		return 1;
#endif

#ifdef CONFIG_SPL_ENV_SUPPORT
	env_init();
	env_load();
	if (env_get_yesno("boot_os") != 1)
		return 1;
#endif

	return 0;
}
#endif

const struct dpll_params *get_dpll_ddr_params(void)
{
	int ind = get_sys_clk_index();

	return &dpll_ddr3_400MHz[ind];
}

const struct dpll_params *get_dpll_mpu_params(void)
{
	int ind = get_sys_clk_index();
	int freq = am335x_get_efuse_mpu_max_freq(cdev);

	switch (freq) {
	case MPUPLL_M_1000:
		return &dpll_mpu_opp[ind][5];
	case MPUPLL_M_800:
		return &dpll_mpu_opp[ind][4];
	case MPUPLL_M_720:
		return &dpll_mpu_opp[ind][3];
	case MPUPLL_M_600:
		return &dpll_mpu_opp[ind][2];
	case MPUPLL_M_500:
		return &dpll_mpu_opp100;
	case MPUPLL_M_300:
		return &dpll_mpu_opp[ind][0];
	}

	return &dpll_mpu_opp[ind][0];
}

void scale_vcores_generic(int freq)
{
	int sil_rev, mpu_vdd;

	/*
	 * VAR-SOM-AM33 usea a TPS65910 PMIC.  For all
	 * MPU frequencies we support we use a CORE voltage of
	 * 1.10V.  For MPU voltage we need to switch based on
	 * the frequency we are running at.
	 */
#ifndef CONFIG_DM_I2C
	i2c_set_bus_num(1);
	if (i2c_probe(TPS65910_CTRL_I2C_ADDR))
		return;
#else
	if (power_tps65910_init(1))
		return;
#endif
	/*
	 * Depending on MPU clock and PG we will need a different
	 * VDD to drive at that speed.
	 */
	sil_rev = readl(&cdev->deviceid) >> 28;
	mpu_vdd = am335x_get_tps65910_mpu_vdd(sil_rev, freq);

	/* Tell the TPS65910 to use i2c */
	tps65910_set_i2c_control();

	/* First update MPU voltage. */
	if (tps65910_voltage_update(MPU, mpu_vdd))
		return;

	/* Second, update the CORE voltage. */
	if (tps65910_voltage_update(CORE, TPS65910_OP_REG_SEL_1_1_0))
		return;

}

void gpi2c_init(void)
{
	/* When needed to be invoked prior to BSS initialization */
	static bool first_time = true;

	if (first_time) {
		enable_i2c1_pin_mux();
#ifndef CONFIG_DM_I2C
		i2c_init(CONFIG_SYS_OMAP24_I2C_SPEED,
			 CONFIG_SYS_OMAP24_I2C_SLAVE);
#endif
		first_time = false;
	}
}

void scale_vcores(void)
{
	int freq;

	gpi2c_init();
	freq = am335x_get_efuse_mpu_max_freq(cdev);

	scale_vcores_generic(freq);
}

void set_uart_mux_conf(void)
{
	enable_uart0_pin_mux();
}

void set_mux_conf_regs(void)
{
	enable_board_pin_mux();

	/* Reset the RMII ethernet chip.
	 */
	gpio_request(GPIO_PHY1_RST, "phy1_rst");
	gpio_direction_output(GPIO_PHY1_RST, 1);
	udelay(10000);
	gpio_set_value(GPIO_PHY1_RST, 0);
	udelay(10000);
	gpio_set_value(GPIO_PHY1_RST, 1);

	enable_rmii1_pin_mux();

	/* Reset the RGMII ethernet chip.
	 */
	gpio_request(GPIO_PHY2_RST, "phy2_rst");
	gpio_request(55, "rgmii_phyaddr2");
	gpio_request(56, "rgmii_mode0");
	gpio_request(57, "rgmii_mode1");
	gpio_request(58, "rgmii_mode2");
	gpio_request(59, "rgmii_mode3");
	gpio_request(49, "rgmii_clk125_ena");

	gpio_direction_output(55, 1);
	gpio_direction_output(56, 1);
	gpio_direction_output(57, 1);
	gpio_direction_output(58, 1);
	gpio_direction_output(59, 1);
	gpio_direction_output(49, 1);
	gpio_direction_output(GPIO_PHY2_RST, 1);

	udelay(10000);
	gpio_set_value(GPIO_PHY2_RST, 0);
	udelay(10000);
	gpio_set_value(GPIO_PHY2_RST, 1);
	udelay(10000);

	enable_rgmii2_pin_mux();
}

const struct ctrl_ioregs ioregs_am335x_var_som = {
	.cm0ioctl		= MT41K256M16HA125E_IOCTRL_VALUE,
	.cm1ioctl		= MT41K256M16HA125E_IOCTRL_VALUE,
	.cm2ioctl		= MT41K256M16HA125E_IOCTRL_VALUE,
	.dt0ioctl		= MT41K256M16HA125E_IOCTRL_VALUE,
	.dt1ioctl		= MT41K256M16HA125E_IOCTRL_VALUE,
};

void sdram_init(void)
{
	config_ddr(400, &ioregs_am335x_var_som,
		   &ddr3_am335x_var_som_data,
		   &ddr3_am335x_var_som_cmd_ctrl_data,
		   &ddr3_am335x_var_som_emif_reg_data, 0);

	/* Fine-tune delay after config_ddr().*/
	udelay(500);
}
#endif

#if defined(CONFIG_OF_BOARD_SETUP) && defined(CONFIG_OF_CONTROL)

/* At the moment, we do not want to stop booting for any failures here */
int ft_board_setup(void *fdt, struct bd_info *bd)
{
	return 0;
}

#endif

/*
 * Basic board specific setup.  Pinmux has been handled already.
 */
int board_init(void)
{
#if !defined(CONFIG_SPL_BUILD)
	int som_rev = -1;
	int som_rev_major = 0;
	int som_rev_minor = 0;
#endif

#if defined(CONFIG_HW_WATCHDOG)
	hw_watchdog_init();
#endif

	gd->bd->bi_boot_params = CONFIG_SYS_SDRAM_BASE + 0x100;
#if defined(CONFIG_NOR) || defined(CONFIG_MTD_RAW_NAND)
	gpmc_init();
#endif

#if !defined(CONFIG_SPL_BUILD)
	gpio_request(GPIO_SOM_REV_BIT0_GPIO, "som_rev_bit_0");
	gpio_direction_input(GPIO_SOM_REV_BIT0_GPIO);
	gpio_request(GPIO_SOM_REV_BIT1_GPIO, "som_rev_bit_1");
	gpio_direction_input(GPIO_SOM_REV_BIT1_GPIO);
	gpio_request(GPIO_SOM_REV_BIT2_GPIO, "som_rev_bit_2");
	gpio_direction_input(GPIO_SOM_REV_BIT2_GPIO);

	som_rev = (gpio_get_value(GPIO_SOM_REV_BIT0_GPIO) |
			(gpio_get_value(GPIO_SOM_REV_BIT1_GPIO) << 1)) +
		!gpio_get_value(GPIO_SOM_REV_BIT2_GPIO);

	gpio_free(GPIO_SOM_REV_BIT0_GPIO);
	gpio_free(GPIO_SOM_REV_BIT1_GPIO);
	gpio_free(GPIO_SOM_REV_BIT2_GPIO);

	/*
	2 = rev 1.2
	3 = rev 1.3
	4 = rev 2.1
	*/
	switch (som_rev)
	{
	case 2:
		som_rev_major = 1;
		som_rev_minor = 2;
		break;
	case 3:
		som_rev_major = 1;
		som_rev_minor = 3;
		break;
	case 4:
		som_rev_major = 2;
		som_rev_minor = 1;
		break;
	default:
		som_rev_major = -1;
		som_rev_minor = -1;
	}

	if (som_rev > 0)
		printf("Variscite AM33 SOM revision %d.%d detected\n",
				som_rev_major, som_rev_minor);
	else {
		printf("ERROR: unknown Variscite AM33X SOM revision.\n");
		hang();
	}

	/* Turn off LCD */
	gpio_request(GPIO_LCD_BACKLIGHT, "backlight");
	gpio_direction_output(GPIO_LCD_BACKLIGHT, 0);

	/* mux bluetooth to omap */
	gpio_request(GPIO_BT_UART_SELECT, "bt_uart_select");
	gpio_direction_output(GPIO_BT_UART_SELECT, 1);
#endif

	return 0;
}

#ifdef CONFIG_BOARD_LATE_INIT
int board_late_init(void)
{
	struct udevice *dev;
#if !defined(CONFIG_SPL_BUILD)
	uint8_t mac_addr[6];
	uint32_t mac_hi, mac_lo;
#endif

#ifdef CONFIG_ENV_VARS_UBOOT_RUNTIME_CONFIG
	/*
	 * Default FIT boot on HS devices. Non FIT images are not allowed
	 * on HS devices.
	 */
	if (get_device_type() == HS_DEVICE)
		env_set("boot_fit", "1");
#endif

#if !defined(CONFIG_SPL_BUILD)
	/* try reading mac address from efuse */
	mac_lo = readl(&cdev->macid0l);
	mac_hi = readl(&cdev->macid0h);
	mac_addr[0] = mac_hi & 0xFF;
	mac_addr[1] = (mac_hi & 0xFF00) >> 8;
	mac_addr[2] = (mac_hi & 0xFF0000) >> 16;
	mac_addr[3] = (mac_hi & 0xFF000000) >> 24;
	mac_addr[4] = mac_lo & 0xFF;
	mac_addr[5] = (mac_lo & 0xFF00) >> 8;

	if (!env_get("ethaddr")) {
		printf("<ethaddr> not set. Validating first E-fuse MAC\n");

		if (is_valid_ethaddr(mac_addr))
			eth_env_set_enetaddr("ethaddr", mac_addr);
	}

	mac_lo = readl(&cdev->macid1l);
	mac_hi = readl(&cdev->macid1h);
	mac_addr[0] = mac_hi & 0xFF;
	mac_addr[1] = (mac_hi & 0xFF00) >> 8;
	mac_addr[2] = (mac_hi & 0xFF0000) >> 16;
	mac_addr[3] = (mac_hi & 0xFF000000) >> 24;
	mac_addr[4] = mac_lo & 0xFF;
	mac_addr[5] = (mac_lo & 0xFF00) >> 8;

	if (!env_get("eth1addr")) {
		if (is_valid_ethaddr(mac_addr))
			eth_env_set_enetaddr("eth1addr", mac_addr);
	}
#endif

	if (!env_get("serial#")) {
		char *board_serial = env_get("board_serial");
		char *ethaddr = env_get("ethaddr");

		if (!board_serial || !strncmp(board_serial, "unknown", 7))
			env_set("serial#", ethaddr);
		else
			env_set("serial#", board_serial);
	}

	/* Just probe the potentially supported cdce913 device */
	uclass_get_device(UCLASS_CLK, 0, &dev);

	return 0;
}
#endif

int board_phy_config(struct phy_device *phydev)
{
	if (phydev->drv->config)
		phydev->drv->config(phydev);

	if (phydev->drv->uid == 0x221550 || phydev->drv->uid == 0x221560) {
		printf ("Found PHY %s\n", phydev->drv->name);
		/* KS8051/KS8081 fixup */
		/* override strap, set RMII mode */
		phy_write(phydev, MDIO_DEVAD_NONE, 0x16, 0x2);
	} else if (phydev->drv->uid == 0x221610) {
		printf ("Found PHY %s\n", phydev->drv->name);
		/* KSZ9021 fixup */
		/* Fine-tune RX data pad skew */
		phy_write(phydev, MDIO_DEVAD_NONE, 0xb, 0x8104);
		phy_write(phydev, MDIO_DEVAD_NONE, 0xc, 0xA097);

		phy_write(phydev, MDIO_DEVAD_NONE, 0xb, 0x8105);
		phy_write(phydev, MDIO_DEVAD_NONE, 0xc, 0);
	} else {
		printf ("Found unexpected PHY 0x%08x\n", phydev->phy_id);
	}

	return 0;
}

/* CPSW platdata */
#if !CONFIG_IS_ENABLED(OF_CONTROL)
struct cpsw_slave_data slave_data[] = {
	{
		.slave_reg_ofs  = CPSW_SLAVE1_OFFSET,
		.sliver_reg_ofs = CPSW_SLIVER1_OFFSET,
		.phy_addr       = 7,
	},
	{
		.slave_reg_ofs  = CPSW_SLAVE0_OFFSET,
		.sliver_reg_ofs = CPSW_SLIVER0_OFFSET,
		.phy_addr       = 1,
	},
};

struct cpsw_platform_data am335_eth_data = {
	.cpsw_base		= CPSW_BASE,
	.version		= CPSW_CTRL_VERSION_2,
	.bd_ram_ofs		= CPSW_BD_OFFSET,
	.ale_reg_ofs		= CPSW_ALE_OFFSET,
	.cpdma_reg_ofs		= CPSW_CPDMA_OFFSET,
	.mdio_div		= CPSW_MDIO_DIV,
	.host_port_reg_ofs	= CPSW_HOST_PORT_OFFSET,
	.channels		= 8,
	.slaves			= 2,
	.slave_data		= slave_data,
	.ale_entries		= 1024,
	.mac_control		= 0x20,
	.active_slave		= 0,
	.mdio_base		= 0x4a101000,
	.gmii_sel		= 0x44e10650,
	.phy_sel_compat		= "ti,am3352-cpsw-phy-sel",
	.syscon_addr		= 0x44e10630,
	.macid_sel_compat	= "cpsw,am33xx",
};

struct eth_pdata cpsw_pdata = {
	.iobase = 0x4a100000,
	.phy_interface = 0,
	.priv_pdata = &am335_eth_data,
};

U_BOOT_DEVICE(am335x_eth) = {
	.name = "eth_cpsw",
	.platdata = &cpsw_pdata,
};
#endif

#ifdef CONFIG_SPL_LOAD_FIT
int board_fit_config_name_match(const char *name)
{
	if (!strcmp(name, "am335x-var-som"))
		return 0;
	else
		return -1;
}
#endif

#ifdef CONFIG_TI_SECURE_DEVICE
void board_fit_image_post_process(const void *fit, int node, void **p_image,
				  size_t *p_size)
{
	secure_boot_verify_image(p_image, p_size);
}
#endif

#if !CONFIG_IS_ENABLED(OF_CONTROL)
static const struct omap_hsmmc_plat am335x_mmc0_platdata = {
	.base_addr = (struct hsmmc *)OMAP_HSMMC1_BASE,
	.cfg.host_caps = MMC_MODE_HS_52MHz | MMC_MODE_HS | MMC_MODE_4BIT,
	.cfg.f_min = 400000,
	.cfg.f_max = 52000000,
	.cfg.voltages = MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_165_195,
	.cfg.b_max = CONFIG_SYS_MMC_MAX_BLK_COUNT,
};

U_BOOT_DEVICE(am335x_mmc0) = {
	.name = "omap_hsmmc",
	.platdata = &am335x_mmc0_platdata,
};

static const struct omap_hsmmc_plat am335x_mmc1_platdata = {
	.base_addr = (struct hsmmc *)OMAP_HSMMC2_BASE,
	.cfg.host_caps = MMC_MODE_HS_52MHz | MMC_MODE_HS | MMC_MODE_8BIT,
	.cfg.f_min = 400000,
	.cfg.f_max = 52000000,
	.cfg.voltages = MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_165_195,
	.cfg.b_max = CONFIG_SYS_MMC_MAX_BLK_COUNT,
};

U_BOOT_DEVICE(am335x_mmc1) = {
	.name = "omap_hsmmc",
	.platdata = &am335x_mmc1_platdata,
};
#endif
