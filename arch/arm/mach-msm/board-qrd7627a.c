/* Copyright (c) 2011-2012, The Linux Foundation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio_event.h>
#include <linux/usb/android.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/i2c.h>
#include <linux/android_pmem.h>
#include <linux/bootmem.h>
#include <linux/mfd/marimba.h>
#include <linux/power_supply.h>
#include <linux/input/rmi_platformdata.h>
#include <linux/input/rmi_i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/memblock.h>
#include <linux/msm_adc.h>
#include <linux/regulator/msm-gpio-regulator.h>
#include <linux/ion.h>
#include <linux/i2c-gpio.h>
#include <linux/regulator/onsemi-ncp6335d.h>
#include <linux/regulator/fan53555.h>
#include <linux/dma-contiguous.h>
#include <linux/dma-mapping.h>
#include <asm/mach/mmc.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <mach/board.h>
#include <mach/gpiomux.h>
#include <mach/msm_iomap.h>
#include <mach/msm_hsusb.h>
#include <mach/rpc_hsusb.h>
#include <mach/rpc_pmapp.h>
#include <mach/usbdiag.h>
#include <mach/msm_memtypes.h>
#include <mach/msm_serial_hs.h>
#include <mach/pmic.h>
#include <mach/socinfo.h>
#include <mach/vreg.h>
#include <mach/rpc_pmapp.h>
#include <mach/msm_battery.h>
#include <mach/rpc_server_handset.h>
#include <mach/socinfo.h>
#include <mach/oem_rapi_client.h>
#include "board-msm7x27a-regulator.h"
#include "devices.h"
#include "devices-msm7x2xa.h"
#include "pm.h"
#include "timer.h"
#include "pm-boot.h"
#include "board-msm7x27a-regulator.h"
#include "board-msm7627a.h"
#include "board-msm7627a-sensor.h"

#define PMEM_KERNEL_EBI1_SIZE	0x3A000
#define MSM_PMEM_AUDIO_SIZE	  0x1F0000
#define BOOTLOADER_BASE_ADDR  0x10000

static struct msm_gpio qup_i2c_gpios_io[] = {
	{ GPIO_CFG(60, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_8MA),
		"qup_scl" },
	{ GPIO_CFG(61, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_8MA),
		"qup_sda" },
	{ GPIO_CFG(131, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_16MA),
		"qup_scl" },
	{ GPIO_CFG(132, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_16MA),
		"qup_sda" },
};

static struct msm_gpio qup_i2c_gpios_hw[] = {
	{ GPIO_CFG(60, 1, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_8MA),
		"qup_scl" },
	{ GPIO_CFG(61, 1, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_8MA),
		"qup_sda" },
	{ GPIO_CFG(131, 2, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_16MA),
		"qup_scl" },
	{ GPIO_CFG(132, 2, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_16MA),
		"qup_sda" },
};

static int qrd_gpios_request_enable(const struct msm_gpio *table, int size)
{
	int i;
	const struct msm_gpio *g;
	struct gpiomux_setting setting;

	int rc = msm_gpios_request(table, size);

	if (!rc){
		for (i = 0; i < size; i++) {
			g = table + i;
			/* use msm_gpiomux_write which can save old configuration */
			setting.func = GPIO_FUNC(g->gpio_cfg);
			setting.dir = GPIO_DIR(g->gpio_cfg);
			setting.pull = GPIO_PULL(g->gpio_cfg);
			setting.drv = GPIO_DRVSTR(g->gpio_cfg);
			msm_gpiomux_write(GPIO_PIN(g->gpio_cfg), GPIOMUX_ACTIVE, &setting, NULL);
			pr_debug("I2C pin %d func %d dir %d pull %d drvstr %d\n",
				GPIO_PIN(g->gpio_cfg), GPIO_FUNC(g->gpio_cfg),
				GPIO_DIR(g->gpio_cfg), GPIO_PULL(g->gpio_cfg),
				GPIO_DRVSTR(g->gpio_cfg));
		}
	}
	return rc;
}

static void gsbi_qup_i2c_gpio_config(int adap_id, int config_type)
{
	int rc;

	if (adap_id < 0 || adap_id > 1)
		return;

	/* Each adapter gets 2 lines from the table */
	if (config_type)
		rc = qrd_gpios_request_enable(&qup_i2c_gpios_hw[adap_id*2], 2);
	else
		rc = qrd_gpios_request_enable(&qup_i2c_gpios_io[adap_id*2], 2);
	if (rc < 0)
		pr_err("QUP GPIO request/enable failed: %d\n", rc);
}

static struct msm_i2c_platform_data msm_gsbi0_qup_i2c_pdata = {
	.clk_freq		= 40000,
	.msm_i2c_config_gpio	= gsbi_qup_i2c_gpio_config,
};

static struct msm_i2c_platform_data msm_gsbi1_qup_i2c_pdata = {
	.clk_freq		= 40000,
	.msm_i2c_config_gpio	= gsbi_qup_i2c_gpio_config,
};

static struct msm_gpio msm8625q_i2c_gpio_config[] = {
	{ GPIO_CFG(39, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_8MA),
		"qup_scl" },
	{ GPIO_CFG(36, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_8MA),
		"qup_sda" },
};

static struct i2c_gpio_platform_data msm8625q_i2c_gpio_pdata = {
	.scl_pin = 39,
	.sda_pin = 36,
	.udelay = 5, /* 100 Khz */
};

static struct platform_device msm8625q_i2c_gpio = {
	.name	= "i2c-gpio",
	.id	= 2,
	.dev	= {
		.platform_data = &msm8625q_i2c_gpio_pdata,
	}
};

#ifdef CONFIG_ARCH_MSM7X27A
#define MSM_PMEM_MDP_SIZE       0x2300000
#define MSM_PMEM_ADSP_SIZE      0x2000000
#define CAMERA_ZSL_SIZE		(SZ_1M * 60)

#ifdef CONFIG_ION_MSM
#define MSM_ION_HEAP_NUM	5
static struct platform_device ion_dev;
static int msm_ion_camera_size;
static int msm_ion_audio_size;
static int msm_ion_sf_size;
static int msm_ion_camera_size_carving;
#endif
#endif

#define CAMERA_HEAP_BASE        0x0
#ifdef CONFIG_CMA
#define CAMERA_HEAP_TYPE	ION_HEAP_TYPE_DMA
#else
#define CAMERA_HEAP_TYPE	ION_HEAP_TYPE_CARVEOUT
#endif

static struct android_usb_platform_data android_usb_pdata = {
	.update_pid_and_serial_num = usb_diag_update_pid_and_serial_num,
	.cdrom = 1,
};

static struct platform_device android_usb_device = {
	.name	= "android_usb",
	.id	= -1,
	.dev	= {
		.platform_data = &android_usb_pdata,
	},
};

#ifdef CONFIG_USB_EHCI_MSM_72K
static void msm_hsusb_vbus_power(unsigned phy_info, int on)
{
#if 1 //Jean
	int rc = 0;
	unsigned gpio;

	gpio = 17;

	rc = gpio_request(gpio,	"i2c_host_vbus_en");
	if (rc < 0) {
		pr_err("failed to request %d GPIO\n", gpio);
		return;
	}
	gpio_direction_output(gpio, !!on);
	gpio_set_value_cansleep(gpio, !!on);
	gpio_free(gpio);
#endif
}

static struct msm_usb_host_platform_data msm_usb_host_pdata = {
	.phy_info       = (USB_PHY_INTEGRATED | USB_PHY_MODEL_45NM),
};

static void __init msm7627a_init_host(void)
{
	msm_add_host(0, &msm_usb_host_pdata);
}
#endif

#ifdef CONFIG_USB_MSM_OTG_72K
static int hsusb_rpc_connect(int connect)
{
	if (connect)
		return msm_hsusb_rpc_connect();
	else
		return msm_hsusb_rpc_close();
}

static struct regulator *reg_hsusb;
static int msm_hsusb_ldo_init(int init)
{
	int rc = 0;

	if (init) {
		reg_hsusb = regulator_get(NULL, "usb");
		if (IS_ERR(reg_hsusb)) {
			rc = PTR_ERR(reg_hsusb);
			pr_err("%s: could not get regulator: %d\n",
					__func__, rc);
			goto out;
		}

		rc = regulator_set_voltage(reg_hsusb, 3300000, 3300000);
		if (rc) {
			pr_err("%s: could not set voltage: %d\n",
					__func__, rc);
			goto reg_free;
		}

		return 0;
	}
	/* else fall through */
reg_free:
	regulator_put(reg_hsusb);
out:
	reg_hsusb = NULL;
	return rc;
}

static int msm_hsusb_ldo_enable(int enable)
{
	static int ldo_status;

	if (IS_ERR_OR_NULL(reg_hsusb))
		return reg_hsusb ? PTR_ERR(reg_hsusb) : -ENODEV;

	if (ldo_status == enable)
		return 0;

	ldo_status = enable;

	return enable ?
		regulator_enable(reg_hsusb) :
		regulator_disable(reg_hsusb);
}

#ifndef CONFIG_USB_EHCI_MSM_72K
static int msm_hsusb_pmic_notif_init(void (*callback)(int online), int init)
{
	int ret = 0;

	if (init)
		ret = msm_pm_app_rpc_init(callback);
	else
		msm_pm_app_rpc_deinit(callback);

	return ret;
}
#endif

static struct msm_otg_platform_data msm_otg_pdata = {
#ifndef CONFIG_USB_EHCI_MSM_72K
	.pmic_vbus_notif_init	 = msm_hsusb_pmic_notif_init,
#else
	.vbus_power		 = msm_hsusb_vbus_power,
#endif
	.rpc_connect		 = hsusb_rpc_connect,
	.pemp_level		 = PRE_EMPHASIS_WITH_20_PERCENT,
	.cdr_autoreset		 = CDR_AUTO_RESET_DISABLE,
	.drv_ampl		 = HS_DRV_AMPLITUDE_DEFAULT,
	.se1_gating		 = SE1_GATING_DISABLE,
	.ldo_init		 = msm_hsusb_ldo_init,
	.ldo_enable		 = msm_hsusb_ldo_enable,
	.chg_init		 = hsusb_chg_init,
	.chg_connected		 = hsusb_chg_connected,
	.chg_vbus_draw		 = hsusb_chg_vbus_draw,
};
#endif

static struct msm_hsusb_gadget_platform_data msm_gadget_pdata = {
	.is_phy_status_timer_on = 1,
	.prop_chg = 0,
};

#ifdef CONFIG_SERIAL_MSM_HS
static struct msm_serial_hs_platform_data msm_uart_dm1_pdata = {
	.inject_rx_on_wakeup	= 1,
	.rx_to_inject		= 0xFD,
};
#endif


/* 8625 PM platform data */
static struct msm_pm_platform_data
		msm8625_pm_data[MSM_PM_SLEEP_MODE_NR * CONFIG_NR_CPUS] = {
	/* CORE0 entries */
	[MSM_PM_MODE(0, MSM_PM_SLEEP_MODE_POWER_COLLAPSE)] = {
					.idle_supported = 1,
					.suspend_supported = 1,
					.idle_enabled = 0,
					.suspend_enabled = 0,
					.latency = 16000,
					.residency = 20000,
	},

	[MSM_PM_MODE(0, MSM_PM_SLEEP_MODE_POWER_COLLAPSE_NO_XO_SHUTDOWN)] = {
					.idle_supported = 1,
					.suspend_supported = 1,
					.idle_enabled = 0,
					.suspend_enabled = 0,
					.latency = 12000,
					.residency = 20000,
	},

	/* picked latency & redisdency values from 7x30 */
	[MSM_PM_MODE(0, MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE)] = {
					.idle_supported = 1,
					.suspend_supported = 1,
					.idle_enabled = 0,
					.suspend_enabled = 1,
					.latency = 500,
					.residency = 500,
	},

	[MSM_PM_MODE(0, MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT)] = {
					.idle_supported = 1,
					.suspend_supported = 1,
					.idle_enabled = 1,
					.suspend_enabled = 1,
					.latency = 2,
					.residency = 10,
	},

	/* picked latency & redisdency values from 7x30 */
	[MSM_PM_MODE(1, MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE)] = {
					.idle_supported = 1,
					.suspend_supported = 1,
					.idle_enabled = 0,
					.suspend_enabled = 1,
					.latency = 500,
					.residency = 500,
	},

	[MSM_PM_MODE(1, MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT)] = {
					.idle_supported = 1,
					.suspend_supported = 1,
					.idle_enabled = 1,
					.suspend_enabled = 1,
					.latency = 2,
					.residency = 10,
	},

	/* picked latency & redisdency values from 7x30 */
	[MSM_PM_MODE(2, MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE)] = {
					.idle_supported = 1,
					.suspend_supported = 1,
					.idle_enabled = 0,
					.suspend_enabled = 1,
					.latency = 500,
					.residency = 500,
	},

	[MSM_PM_MODE(2, MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT)] = {
					.idle_supported = 1,
					.suspend_supported = 1,
					.idle_enabled = 1,
					.suspend_enabled = 1,
					.latency = 2,
					.residency = 10,
	},

	/* picked latency & redisdency values from 7x30 */
	[MSM_PM_MODE(3, MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE)] = {
					.idle_supported = 1,
					.suspend_supported = 1,
					.idle_enabled = 0,
					.suspend_enabled = 1,
					.latency = 500,
					.residency = 500,
	},

	[MSM_PM_MODE(3, MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT)] = {
					.idle_supported = 1,
					.suspend_supported = 1,
					.idle_enabled = 1,
					.suspend_enabled = 1,
					.latency = 2,
					.residency = 10,
	},

};

static struct msm_pm_boot_platform_data msm_pm_8625_boot_pdata __initdata = {
	.mode = MSM_PM_BOOT_CONFIG_REMAP_BOOT_ADDR,
	.v_addr = MSM_CFG_CTL_BASE,
};

static struct android_pmem_platform_data android_pmem_adsp_pdata = {
	.name = "pmem_adsp",
	.allocator_type = PMEM_ALLOCATORTYPE_BITMAP,
	.cached = 1,
	.memory_type = MEMTYPE_EBI1,
};

static struct platform_device android_pmem_adsp_device = {
	.name = "android_pmem",
	.id = 1,
	.dev = { .platform_data = &android_pmem_adsp_pdata },
};

static unsigned pmem_mdp_size = MSM_PMEM_MDP_SIZE;
static int __init pmem_mdp_size_setup(char *p)
{
	pmem_mdp_size = memparse(p, NULL);
	return 0;
}

early_param("pmem_mdp_size", pmem_mdp_size_setup);

static unsigned pmem_adsp_size = MSM_PMEM_ADSP_SIZE;
static int __init pmem_adsp_size_setup(char *p)
{
	pmem_adsp_size = memparse(p, NULL);
	return 0;
}

early_param("pmem_adsp_size", pmem_adsp_size_setup);

static struct android_pmem_platform_data android_pmem_audio_pdata = {
	.name = "pmem_audio",
	.allocator_type = PMEM_ALLOCATORTYPE_BITMAP,
	.cached = 0,
	.memory_type = MEMTYPE_EBI1,
};

static struct platform_device android_pmem_audio_device = {
	.name = "android_pmem",
	.id = 2,
	.dev = { .platform_data = &android_pmem_audio_pdata },
};

static struct android_pmem_platform_data android_pmem_pdata = {
	.name = "pmem",
	.allocator_type = PMEM_ALLOCATORTYPE_BITMAP,
	.cached = 1,
	.memory_type = MEMTYPE_EBI1,
};
static struct platform_device android_pmem_device = {
	.name = "android_pmem",
	.id = 0,
	.dev = { .platform_data = &android_pmem_pdata },
};

static u32 msm_calculate_batt_capacity(u32 current_voltage);

static struct msm_psy_batt_pdata msm_psy_batt_data = {
	.voltage_min_design     = 3500,
	.voltage_max_design     = 4200,
	.avail_chg_sources      = AC_CHG | USB_CHG | UNKNOWN_CHG,
	.batt_technology        = POWER_SUPPLY_TECHNOLOGY_LION,
	.calculate_capacity     = &msm_calculate_batt_capacity,
};

static u32 msm_calculate_batt_capacity(u32 current_voltage)
{
	const static u32 voltage_to_capacity_table[][2] = {
		{4150, 100},
		{4066,  95},
		{4024,  90},
		{3984,  85},
		{3948,  80},
		{3915,  75},
		{3880,  70},
		{3845,  65},
		{3816,  60},
		{3794,  55},
		{3776,  50},
		{3761,  45},
		{3749,  40},
		{3738,  35},
		{3730,  30},
		{3717,  25},
		{3708,  20},
		{3674,  15},
		{3659,  10},
		{3632,   5},
		{3550,   0},
		{   0,   0},
	};
	int i, base, top, percent_diff;

	if (current_voltage >= voltage_to_capacity_table[0][0])
	{
		return 100;
	}

	for (i = 1; i < ARRAY_SIZE(voltage_to_capacity_table); i++) {
		if (current_voltage >= voltage_to_capacity_table[i][0]) {
			break;
		}
	}

	base = voltage_to_capacity_table[i][0];
	top = voltage_to_capacity_table[i - 1][0];
	percent_diff =  voltage_to_capacity_table[i - 1][1] -
		voltage_to_capacity_table[i][1];
	return voltage_to_capacity_table[i][1] +
		(current_voltage - base) * percent_diff / (top - base);
}

static struct platform_device msm_fastboot_device = {
	.name               = "fastboot",
	.id                 = -1,
};

static struct platform_device msm_batt_device = {
	.name               = "msm-battery",
	.id                 = -1,
	.dev.platform_data  = &msm_psy_batt_data,
};

static char *msm_adc_surf_device_names[] = {
	"XO_ADC",
};

static struct msm_adc_platform_data msm_adc_pdata = {
	.dev_names = msm_adc_surf_device_names,
	.num_adc = ARRAY_SIZE(msm_adc_surf_device_names),
	.target_hw = MSM_8x25,
};

static struct platform_device msm_adc_device = {
	.name   = "msm_adc",
	.id = -1,
	.dev = {
		.platform_data = &msm_adc_pdata,
	},
};

/* Regulator configuration for the NCP6335D buck */
struct regulator_consumer_supply ncp6335d_consumer_supplies[] = {
	REGULATOR_SUPPLY("ncp6335d", NULL),
	/* TO DO: NULL entry needs to be fixed once
	 * we fix the cross-dependencies.
	*/
	REGULATOR_SUPPLY("vddx_cx", NULL),
};

static struct regulator_init_data ncp6335d_init_data = {
	.constraints	= {
		.name		= "ncp6335d_sw",
		.min_uV		= 600000,
		.max_uV		= 1400000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS |
				REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL |
				REGULATOR_MODE_FAST,
		.initial_mode	= REGULATOR_MODE_NORMAL,
		.always_on	= 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(ncp6335d_consumer_supplies),
	.consumer_supplies = ncp6335d_consumer_supplies,
};

static struct ncp6335d_platform_data ncp6335d_pdata = {
	.init_data = &ncp6335d_init_data,
	.default_vsel = NCP6335D_VSEL0,
	.slew_rate_ns = 333,
	.rearm_disable = 1,
};

/* Regulator configuration for the FAN53555 buck */
struct regulator_consumer_supply fan53555_consumer_supplies[] = {
	REGULATOR_SUPPLY("fan53555", NULL),
	/* TO DO: NULL entry needs to be fixed once
	 * we fix the cross-dependencies.
	*/
	REGULATOR_SUPPLY("vddx_cx", NULL),
};

static struct regulator_init_data fan53555_init_data = {
	.constraints	= {
		.name		= "fan53555",
		.min_uV		= 603000,
		.max_uV		= 1411000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS |
				REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL |
				REGULATOR_MODE_FAST,
		.initial_mode	= REGULATOR_MODE_NORMAL,
		.always_on	= 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(fan53555_consumer_supplies),
	.consumer_supplies = fan53555_consumer_supplies,
};

static struct fan53555_platform_data fan53555_pdata = {
	.regulator = &fan53555_init_data,
	.slew_rate = FAN53555_SLEW_RATE_64MV,
	.sleep_vsel_id = FAN53555_VSEL_ID_1,
};

static struct i2c_board_info i2c2_info[] __initdata = {
	{
		I2C_BOARD_INFO("ncp6335d", 0x38 >> 1),
		.platform_data = &ncp6335d_pdata,
	},
	{
		I2C_BOARD_INFO("fan53555", 0xC0 >> 1),
		.platform_data = &fan53555_pdata,
	},
};

static struct platform_device *common_devices[] __initdata = {
	&android_usb_device,
	&android_pmem_device,
	&android_pmem_adsp_device,
	&android_pmem_audio_device,
	&msm_batt_device,
	&msm_device_adspdec,
	&msm_device_snd,
	&msm_device_cad,
	&asoc_msm_pcm,
	&asoc_msm_dai0,
	&asoc_msm_dai1,
	&msm_adc_device,
#ifdef CONFIG_ION_MSM
	&ion_dev,
#endif
	&msm_fastboot_device,
};


static struct platform_device msm_wlan_ar6000_pm_device = {
	.name           = "wlan_ar6000_pm_dev",
	.id             = 1,
	.num_resources  =       0,
        .resource       =       NULL,
};

static struct platform_device *msm8625_evb_devices[] __initdata = {
	&msm8625_device_dmov,
	&msm8625_device_smd,
	&msm8625_gsbi0_qup_i2c_device,
	&msm8625_gsbi1_qup_i2c_device,
	&msm8625_device_uart1,
	&msm8625_device_uart_dm1,
	&msm8625_device_otg,
	&msm8625_device_gadget_peripheral,
	&msm8625_kgsl_3d0,
};

static unsigned pmem_kernel_ebi1_size = PMEM_KERNEL_EBI1_SIZE;
static int __init pmem_kernel_ebi1_size_setup(char *p)
{
	pmem_kernel_ebi1_size = memparse(p, NULL);
	return 0;
}
early_param("pmem_kernel_ebi1_size", pmem_kernel_ebi1_size_setup);

static unsigned pmem_audio_size = MSM_PMEM_AUDIO_SIZE;
static int __init pmem_audio_size_setup(char *p)
{
	pmem_audio_size = memparse(p, NULL);
	return 0;
}
early_param("pmem_audio_size", pmem_audio_size_setup);

static void fix_sizes(void)
{
	if (get_ddr_size() > SZ_512M)
		pmem_adsp_size = CAMERA_ZSL_SIZE;
	else {
		if (machine_is_msm8625q_skue() || machine_is_msm8625q_evbd()
					|| machine_is_msm8625q_skud())
			pmem_mdp_size = 0;
	}

#ifdef CONFIG_ION_MSM
	msm_ion_audio_size = MSM_PMEM_AUDIO_SIZE;
#ifdef CONFIG_CMA
	msm_ion_camera_size = CAMERA_ZSL_SIZE;
	msm_ion_camera_size_carving = 0;
#else
	msm_ion_camera_size = pmem_adsp_size;
	msm_ion_camera_size_carving = msm_ion_camera_size;
#endif
	msm_ion_sf_size = pmem_mdp_size;
#endif
}

#ifdef CONFIG_ION_MSM
#ifdef CONFIG_MSM_MULTIMEDIA_USE_ION
static struct ion_co_heap_pdata co_ion_pdata = {
	.adjacent_mem_id = INVALID_HEAP_ID,
	.align = PAGE_SIZE,
};

static struct ion_co_heap_pdata co_mm_ion_pdata = {
	.adjacent_mem_id = INVALID_HEAP_ID,
	.align = PAGE_SIZE,
};

static u64 msm_dmamask = DMA_BIT_MASK(32);

static struct platform_device ion_cma_device = {
	.name = "ion-cma-device",
	.id = -1,
	.dev = {
		.dma_mask = &msm_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	}
};
#endif

/**
 * These heaps are listed in the order they will be allocated.
 * Don't swap the order unless you know what you are doing!
 */
struct ion_platform_heap msm7627a_heaps[] = {
		{
			.id	= ION_SYSTEM_HEAP_ID,
			.type	= ION_HEAP_TYPE_SYSTEM,
			.name	= ION_VMALLOC_HEAP_NAME,
		},
#ifdef CONFIG_MSM_MULTIMEDIA_USE_ION
		/* PMEM_ADSP = CAMERA */
		{
			.id	= ION_CAMERA_HEAP_ID,
			.type	= CAMERA_HEAP_TYPE,
			.name	= ION_CAMERA_HEAP_NAME,
			.memory_type = ION_EBI_TYPE,
			.extra_data = (void *)&co_mm_ion_pdata,
			.priv	= (void *)&ion_cma_device.dev,
		},
		/* AUDIO HEAP 1*/
		{
			.id	= ION_AUDIO_HEAP_ID,
			.type	= ION_HEAP_TYPE_CARVEOUT,
			.name	= ION_AUDIO_HEAP_NAME,
			.memory_type = ION_EBI_TYPE,
			.extra_data = (void *)&co_ion_pdata,
		},
		/* PMEM_MDP = SF */
		{
			.id	= ION_SF_HEAP_ID,
			.type	= ION_HEAP_TYPE_CARVEOUT,
			.name	= ION_SF_HEAP_NAME,
			.memory_type = ION_EBI_TYPE,
			.extra_data = (void *)&co_ion_pdata,
		},
		/* AUDIO HEAP 2*/
		{
			.id    = ION_AUDIO_HEAP_BL_ID,
			.type  = ION_HEAP_TYPE_CARVEOUT,
			.name  = ION_AUDIO_BL_HEAP_NAME,
			.memory_type = ION_EBI_TYPE,
			.extra_data = (void *)&co_ion_pdata,
			.base = BOOTLOADER_BASE_ADDR,
		},

#endif
};

static struct ion_platform_data ion_pdata = {
	.nr = MSM_ION_HEAP_NUM,
	.has_outer_cache = 1,
	.heaps = msm7627a_heaps,
};

static struct platform_device ion_dev = {
	.name = "ion-msm",
	.id = 1,
	.dev = { .platform_data = &ion_pdata },
};
#endif

static struct memtype_reserve msm7627a_reserve_table[] __initdata = {
	[MEMTYPE_SMI] = {
	},
	[MEMTYPE_EBI0] = {
		.flags	=	MEMTYPE_FLAGS_1M_ALIGN,
	},
	[MEMTYPE_EBI1] = {
		.flags	=	MEMTYPE_FLAGS_1M_ALIGN,
	},
};

#ifdef CONFIG_ANDROID_PMEM
#ifndef CONFIG_MSM_MULTIMEDIA_USE_ION
static struct android_pmem_platform_data *pmem_pdata_array[] __initdata = {
		&android_pmem_adsp_pdata,
		&android_pmem_audio_pdata,
		&android_pmem_pdata,
};
#endif
#endif

static void __init size_pmem_devices(void)
{
#ifdef CONFIG_ANDROID_PMEM
#ifndef CONFIG_MSM_MULTIMEDIA_USE_ION
	unsigned int i;

	android_pmem_adsp_pdata.size = pmem_adsp_size;
	android_pmem_pdata.size = pmem_mdp_size;
	android_pmem_audio_pdata.size = pmem_audio_size;
#endif
#endif
}

#ifdef CONFIG_ANDROID_PMEM
#ifndef CONFIG_MSM_MULTIMEDIA_USE_ION
static void __init reserve_memory_for(struct android_pmem_platform_data *p)
{
	msm7627a_reserve_table[p->memory_type].size += p->size;
}
#endif
#endif

static void __init reserve_pmem_memory(void)
{
#ifdef CONFIG_ANDROID_PMEM
#ifndef CONFIG_MSM_MULTIMEDIA_USE_ION
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(pmem_pdata_array); ++i)
		reserve_memory_for(pmem_pdata_array[i]);

	msm7627a_reserve_table[MEMTYPE_EBI1].size += pmem_kernel_ebi1_size;
#endif
#endif
}

static void __init size_ion_devices(void)
{
#ifdef CONFIG_MSM_MULTIMEDIA_USE_ION
	ion_pdata.heaps[1].size = msm_ion_camera_size;
	ion_pdata.heaps[2].size = PMEM_KERNEL_EBI1_SIZE;
	ion_pdata.heaps[3].size = msm_ion_sf_size;
	ion_pdata.heaps[4].size = msm_ion_audio_size;
#endif
}

static void __init reserve_ion_memory(void)
{
#if defined(CONFIG_ION_MSM) && defined(CONFIG_MSM_MULTIMEDIA_USE_ION)
	msm7627a_reserve_table[MEMTYPE_EBI1].size += PMEM_KERNEL_EBI1_SIZE;
	msm7627a_reserve_table[MEMTYPE_EBI1].size += msm_ion_sf_size;
	msm7627a_reserve_table[MEMTYPE_EBI1].size +=
		msm_ion_camera_size_carving;
#endif
}

static void __init msm7627a_calculate_reserve_sizes(void)
{
	fix_sizes();
	size_pmem_devices();
	reserve_pmem_memory();
	size_ion_devices();
	reserve_ion_memory();
}

static int msm7627a_paddr_to_memtype(unsigned int paddr)
{
	return MEMTYPE_EBI1;
}

static struct reserve_info msm7627a_reserve_info __initdata = {
	.memtype_reserve_table = msm7627a_reserve_table,
	.calculate_reserve_sizes = msm7627a_calculate_reserve_sizes,
	.paddr_to_memtype = msm7627a_paddr_to_memtype,
};

static void __init msm7627a_reserve(void)
{
	reserve_info = &msm7627a_reserve_info;
	memblock_remove(MSM8625_NON_CACHE_MEM, SZ_2K);
	memblock_remove(BOOTLOADER_BASE_ADDR, msm_ion_audio_size);
	msm_reserve();
#ifdef CONFIG_CMA
	dma_declare_contiguous(
			&ion_cma_device.dev,
			msm_ion_camera_size,
			CAMERA_HEAP_BASE,
			0x26000000);
#endif
}

static void __init msm8625_reserve(void)
{
	memblock_remove(MSM8625_CPU_PHYS, SZ_8);
	memblock_remove(MSM8625_WARM_BOOT_PHYS, SZ_32);
	msm7627a_reserve();
}

static void msmqrd_adsp_add_pdev(void)
{
	int rc = 0;
	struct rpc_board_dev *rpc_adsp_pdev;

	rpc_adsp_pdev = kzalloc(sizeof(struct rpc_board_dev), GFP_KERNEL);
	if (rpc_adsp_pdev == NULL) {
		pr_err("%s: Memory Allocation failure\n", __func__);
		return;
	}

	rpc_adsp_pdev->prog = ADSP_RPC_PROG;
	rpc_adsp_pdev->pdev = msm8625_device_adsp;

	rc = msm_rpc_add_board_dev(rpc_adsp_pdev, 1);
	if (rc < 0) {
		pr_err("%s: return val: %d\n",	__func__, rc);
		kfree(rpc_adsp_pdev);
	}
}

static void __init msm8625_device_i2c_init(void)
{
	int i, rc;

	msm8625_gsbi0_qup_i2c_device.dev.platform_data
					= &msm_gsbi0_qup_i2c_pdata;
	msm8625_gsbi1_qup_i2c_device.dev.platform_data
					= &msm_gsbi1_qup_i2c_pdata;
	if (cpu_is_msm8625q()) {
		for (i = 0 ; i < ARRAY_SIZE(msm8625q_i2c_gpio_config); i++) {
			rc = gpio_tlmm_config(
					msm8625q_i2c_gpio_config[i].gpio_cfg,
					GPIO_CFG_ENABLE);
			if (rc)
				pr_err("I2C-gpio tlmm config failed\n");
		}
		rc = platform_device_register(&msm8625q_i2c_gpio);
		if (rc)
			pr_err("%s: could not register i2c-gpio device: %d\n",
						__func__, rc);
	}
}

static struct platform_device msm_proccomm_regulator_dev = {
	.name   = PROCCOMM_REGULATOR_DEV_NAME,
	.id     = -1,
	.dev    = {
		.platform_data = &msm7x27a_proccomm_regulator_data
	}
};

static void __init msm7627a_init_regulators(void)
{
	int rc = platform_device_register(&msm_proccomm_regulator_dev);
	if (rc)
		pr_err("%s: could not register regulator device: %d\n",
				__func__, rc);
}

#define NV_ITEM_WLAN_MAC_ADDR   4678
extern int msm_read_nv(unsigned int nv_item, void *buf);
unsigned char wlan_mac_addr[6];

static int __init msm_qrd_init_ar6000pm(void)
{
	msm_wlan_ar6000_pm_device.dev.platform_data = &ar600x_wlan_power;
	msm_read_nv(NV_ITEM_WLAN_MAC_ADDR,wlan_mac_addr);
	return platform_device_register(&msm_wlan_ar6000_pm_device);
}

static void __init msm_add_footswitch_devices(void)
{
	platform_add_devices(msm_footswitch_devices,
				msm_num_footswitch_devices);
}

static void __init add_platform_devices(void)
{
	platform_add_devices(msm8625_evb_devices,
			ARRAY_SIZE(msm8625_evb_devices));

	platform_add_devices(common_devices,
			ARRAY_SIZE(common_devices));
}

#define UART1DM_RX_GPIO		45
static void __init qrd7627a_uart1dm_config(void)
{
	msm_uart_dm1_pdata.wakeup_irq = gpio_to_irq(UART1DM_RX_GPIO);
	if (cpu_is_msm8625() || cpu_is_msm8625q())
		msm8625_device_uart_dm1.dev.platform_data =
			&msm_uart_dm1_pdata;
	else
		msm_device_uart_dm1.dev.platform_data = &msm_uart_dm1_pdata;
}

static void __init qrd7627a_otg_gadget(void)
{
	msm_otg_pdata.swfi_latency = msm8625_pm_data[MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT].latency;
	msm8625_device_otg.dev.platform_data = &msm_otg_pdata;
	msm8625_device_gadget_peripheral.dev.platform_data = &msm_gadget_pdata;
}

static void __init msm_pm_init(void)
{
	msm_pm_set_platform_data(msm8625_pm_data,
			ARRAY_SIZE(msm8625_pm_data));
	BUG_ON(msm_pm_boot_init(&msm_pm_8625_boot_pdata));
	msm8x25_spm_device_init();
	msm_pm_register_cpr_ops();
}

static void __init msm_qrd_init(void)
{
	printk("msm_qrd_init,cpu_is_msm8625:%d,cpu_is_msm8625q:%d\n",cpu_is_msm8625(),cpu_is_msm8625q());
	printk("msm_qrd_init,machine_is_msm8625q_skud:%d,machine_is_msm8625_qrd5:%d\n",machine_is_msm8625q_skud(),machine_is_msm8625_qrd5());
	msm7x2x_misc_init();
	msm7627a_init_regulators();
	msmqrd_adsp_add_pdev();

	msm8625_device_i2c_init();

	/* uart1dm*/
	qrd7627a_uart1dm_config();
	/*OTG gadget*/
	qrd7627a_otg_gadget();

	msm_add_footswitch_devices();
	add_platform_devices();

	/* Ensure ar6000pm device is registered before MMC/SDC */
	msm_qrd_init_ar6000pm();
	msm7627a_init_mmc();

#ifdef CONFIG_USB_EHCI_MSM_72K
	msm7627a_init_host();
#endif
	msm_pm_init();

	msm_pm_register_irqs();
	msm_fb_add_devices();

	if (machine_is_msm8625q_evbd() || machine_is_msm8625q_skud()
				       || machine_is_msm8625q_skue())
		i2c_register_board_info(2, i2c2_info,
				ARRAY_SIZE(i2c2_info));

#if defined(CONFIG_BT) && defined(CONFIG_MARIMBA_CORE)
	msm7627a_bt_power_init();
#endif
	msm7627a_sensor_init();
	msm7627a_camera_init();
	msm7627a_add_io_devices();
	msm7x25a_kgsl_3d0_init();
	msm8x25_kgsl_3d0_init();
}

static void __init qrd7627a_init_early(void)
{
	msm_msm7627a_allocate_memory_regions();
}

#ifdef CONFIG_MSM_AMSS_ENHANCE_DEBUG
#define TASK_OFFSET_SEND(member, name)		\
	do {					\
		input.extension.len = 2;	\
		input.extension.data[0] = (uint32_t)TASK_STRUCT_TAG; \
		input.extension.data[1] = offsetof(struct task_struct, member); \
		input.address = (uint32_t)__virt_to_phys((unsigned long)&init_task); \
		input.size = sizeof(struct task_struct); \
		strncpy(input.file_name, name, NZI_ITEM_FILE_NAME_LENGTH); \
		input.file_name[NZI_ITEM_FILE_NAME_LENGTH - 1] = 0; \
		send_modem_logaddr(&input); \
	} while (0)

static int __init qrd7627a_logbuf_init(void)
{
	nzi_buf_item_type input;
	extern char __log_buf[];
	extern unsigned long totalram_pages;
	extern atomic_long_t vm_stat[];

#ifdef CONFIG_PRINTK
	/* send the kernel log address */
	input.extension.len = 0;
	input.address = (uint32_t)__virt_to_phys((unsigned long)__log_buf);
	input.size = (1 << CONFIG_LOG_BUF_SHIFT);
	strncpy(input.file_name, "dmesg", NZI_ITEM_FILE_NAME_LENGTH);
	input.file_name[NZI_ITEM_FILE_NAME_LENGTH - 1] = 0;
	send_modem_logaddr(&input);
#endif

	/* ******struct task_struct part ******/
	TASK_OFFSET_SEND(tasks, "tasks_of");
	TASK_OFFSET_SEND(thread_group, "tg_of");
	TASK_OFFSET_SEND(mm, "mm_of");
	TASK_OFFSET_SEND(comm, "comm_of");
	TASK_OFFSET_SEND(pid, "pid_of");
	TASK_OFFSET_SEND(tgid, "tgid_of");
	/* ******struct task_struct end ******/

	/* totalram_pages */
	input.extension.len = 1;
	input.extension.data[0] = (uint32_t)MEM_INFO_TAG;
	input.address = (uint32_t)__virt_to_phys((unsigned long)&totalram_pages);
	input.size = sizeof(totalram_pages);
	strncpy(input.file_name, "totalram", NZI_ITEM_FILE_NAME_LENGTH);
	input.file_name[NZI_ITEM_FILE_NAME_LENGTH - 1] = 0;
	send_modem_logaddr(&input);

	/* vm_stat[vm_stat[NR_VM_ZONE_STAT_ITEMS]; */
	input.extension.len = 1;
	input.extension.data[0] = (uint32_t)MEM_INFO_TAG;
	input.address = (uint32_t)__virt_to_phys((unsigned long)vm_stat);
	input.size = sizeof(vm_stat);
	strncpy(input.file_name, "vm_stat", NZI_ITEM_FILE_NAME_LENGTH);
	input.file_name[NZI_ITEM_FILE_NAME_LENGTH - 1] = 0;
	send_modem_logaddr(&input);

	return 0;
}
late_initcall(qrd7627a_logbuf_init);
#endif

MACHINE_START(MSM7627A_QRD1, "QRD MSM7627a QRD1")
	.atag_offset	= 0x100,
	.map_io		= msm_common_io_init,
	.reserve	= msm7627a_reserve,
	.init_irq	= msm_init_irq,
	.init_machine	= msm_qrd_init,
	.timer		= &msm_timer,
	.init_early	= qrd7627a_init_early,
	.handle_irq	= vic_handle_irq,
MACHINE_END
MACHINE_START(MSM7627A_QRD3, "QRD MSM7627a QRD3")
	.atag_offset	= 0x100,
	.map_io		= msm_common_io_init,
	.reserve	= msm7627a_reserve,
	.init_irq	= msm_init_irq,
	.init_machine	= msm_qrd_init,
	.timer		= &msm_timer,
	.init_early	= qrd7627a_init_early,
	.handle_irq	= vic_handle_irq,
MACHINE_END
MACHINE_START(MSM7627A_EVB, "QRD MSM7627a EVB")
	.atag_offset	= 0x100,
	.map_io		= msm_common_io_init,
	.reserve	= msm7627a_reserve,
	.init_irq	= msm_init_irq,
	.init_machine	= msm_qrd_init,
	.timer		= &msm_timer,
	.init_early	= qrd7627a_init_early,
	.handle_irq	= vic_handle_irq,
MACHINE_END
MACHINE_START(MSM8625_EVB, "QRD MSM8625 EVB")
	.atag_offset	= 0x100,
	.map_io		= msm8625_map_io,
	.reserve	= msm8625_reserve,
	.init_irq	= msm8625_init_irq,
	.init_machine	= msm_qrd_init,
	.timer		= &msm_timer,
	.init_early	= qrd7627a_init_early,
	.handle_irq	= gic_handle_irq,
MACHINE_END

MACHINE_START(MSM7X27A_QRD5A, "QRD MSM7x27A QRD5A")
	.atag_offset	= 0x100,
	.map_io		= msm_common_io_init,
	.reserve	= msm7627a_reserve,
	.init_irq	= msm_init_irq,
	.init_machine	= msm_qrd_init,
	.timer		= &msm_timer,
	.init_early	= qrd7627a_init_early,
	.handle_irq	= vic_handle_irq,
MACHINE_END

MACHINE_START(MSM8625_QRD7, "QRD MSM8625 QRD7")
	.atag_offset	= 0x100,
	.map_io		= msm8625_map_io,
	.reserve	= msm8625_reserve,
	.init_irq	= msm8625_init_irq,
	.init_machine	= msm_qrd_init,
	.timer		= &msm_timer,
	.init_early	= qrd7627a_init_early,
	.handle_irq	= gic_handle_irq,
MACHINE_END
MACHINE_START(MSM8625_QRD5, "QRD MSM8625 QRD5")
	.atag_offset	= 0x100,
	.map_io		= msm8625_map_io,
	.reserve	= msm8625_reserve,
	.init_irq	= msm8625_init_irq,
	.init_machine	= msm_qrd_init,
	.timer		= &msm_timer,
	.init_early	= qrd7627a_init_early,
	.handle_irq	= gic_handle_irq,
MACHINE_END

MACHINE_START(MSM8625Q_EVBD, "QRD MSM8625Q EVBD")
	.atag_offset	= 0x100,
	.map_io		= msm8625_map_io,
	.reserve	= msm8625_reserve,
	.init_irq	= msm8625_init_irq,
	.init_machine	= msm_qrd_init,
	.timer		= &msm_timer,
	.init_early	= qrd7627a_init_early,
	.handle_irq	= gic_handle_irq,
MACHINE_END
MACHINE_START(MSM8625Q_SKUD, "QRD MSM8625Q SKUD")
	.atag_offset	= 0x100,
	.map_io		= msm8625_map_io,
	.reserve	= msm8625_reserve,
	.init_irq	= msm8625_init_irq,
	.init_machine	= msm_qrd_init,
	.timer		= &msm_timer,
	.init_early	= qrd7627a_init_early,
	.handle_irq	= gic_handle_irq,
MACHINE_END

MACHINE_START(MSM8625Q_SKUE, "QRD MSM8625Q SKUE")
	.atag_offset	= 0x100,
	.map_io		= msm8625_map_io,
	.reserve	= msm8625_reserve,
	.init_irq	= msm8625_init_irq,
	.init_machine	= msm_qrd_init,
	.timer		= &msm_timer,
	.init_early	= qrd7627a_init_early,
	.handle_irq	= gic_handle_irq,
MACHINE_END
