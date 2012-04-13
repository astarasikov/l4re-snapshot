/*
 * board-overo.c (Gumstix Overo)
 *
 * Initial code: Steve Sakoman <steve@sakoman.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/opp.h>
#include <linux/platform_device.h>
#include <linux/i2c/twl.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/spi/spi.h>

//#include <linux/mtd/mtd.h>
//#include <linux/mtd/nand.h>
//#include <linux/mtd/partitions.h>
//#include <linux/mmc/host.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>

#include <plat/board.h>
#include <plat/common.h>
#include <plat/omap_device.h>
//#include <video/omapdss.h>
//#include <video/omap-panel-generic-dpi.h>
//#include <plat/gpmc.h>
#include <plat/hardware.h>
//#include <plat/nand.h>
//#include <plat/mcspi.h>
#include <plat/mux.h>
#include <plat/usb.h>
#include <plat/irqs.h>
#include <plat/io.h>

#include "mux.h"
#include "pm.h"
//#include "sdram-micron-mt46h32m32lf-6.h"
//#include "hsmmc.h"
#include <plat/omap34xx.h>
#include "common-board-devices.h"

static struct twl4030_usb_data overo_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};


static struct twl4030_platform_data overo_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,
	.gpio		= NULL,
	.madc		= NULL,
	.usb		= &overo_usb_data,
	.codec		= NULL,
	.vmmc1		= NULL,
	.vdac		= NULL,
	.vpll2		= NULL,
};

static int overo_i2c_init(void)
{
	omap3_pmic_init("tps65950", &overo_twldata);
	/* i2c2 pins are used for gpio */
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static struct omap_globals omap3_globals = {
        .class  = OMAP343X_CLASS,
        .tap    = OMAP2_L4_IO_ADDRESS(0x4830A000),
        .sdrc   = OMAP343X_SDRC_BASE,
        .sms    = OMAP343X_SMS_BASE,
        .ctrl   = OMAP343X_CTRL_BASE,
        .prm    = OMAP3430_PRM_BASE,
        .cm     = OMAP3430_CM_BASE,
};

static void omap2_set_globals_3xxx(void)
{
        //omap2_set_globals_tap(&omap3_globals);
        //omap2_set_globals_sdrc(&omap3_globals);
        omap2_set_globals_control(&omap3_globals);
        omap2_set_globals_prcm(&omap3_globals);
}

extern int omap_hwmod_setup_all(void);
extern int omap2_common_pm_init(void);

void overo_init_early(void)
{
        omap2_set_globals_3xxx();
        omap34xx_map_common_io();

        omap_init_irq();
	omap2_init_common_infrastructure();
	omap2_init_common_devices(NULL,NULL);
	omap_hwmod_setup_all();

	omap2_common_pm_init();
}

#if 0
static const struct usbhs_omap_board_data usbhs_bdata __initconst = {
	.port_mode[0] = OMAP_USBHS_PORT_MODE_UNUSED,
	.port_mode[1] = OMAP_EHCI_PORT_MODE_PHY,
	.port_mode[2] = OMAP_USBHS_PORT_MODE_UNUSED,
	.phy_reset  = true,
	.reset_gpio_port[0]  = -EINVAL,
	.reset_gpio_port[1]  = OVERO_GPIO_USBH_NRESET,
	.reset_gpio_port[2]  = -EINVAL
};
#endif

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[]  = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif


static void overo_opp_init(void)
{
	int r = 0;

	/* Initialize the omap3 opp table */
	if (omap3_opp_init()) {
		pr_err("%s: opp default init failed\n", __func__);
		return;
	}

	return;
}

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_PERIPHERAL,
	.power			= 100,
};

extern u32 omap2_cm_read_mod_reg(s16 module, u16 idx);
extern void omap2_cm_write_mod_reg(u32 val, s16 module, u16 idx);
extern u32 omap_ctrl_readl(u16 offset);

void overo_init(void)
{
	int ret;

	//omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	overo_i2c_init();
	//omap_display_init(&overo_dss_data);
	//omap_serial_init();
	//omap_nand_flash_init(0, overo_nand_partitions,
	//		     ARRAY_SIZE(overo_nand_partitions));
	usb_musb_init(&musb_board_data);
	//usbhs_init(&usbhs_bdata);
	//overo_spi_init();
	//overo_init_smsc911x();
	//overo_display_init();
	//overo_init_led();
	//overo_init_keys();
	overo_opp_init();

	printk("\n\n\n============\n");
        printk("ICLKEN_CORE %x\n", omap2_cm_read_mod_reg(0x200,0x10));
        omap2_cm_write_mod_reg(0xffffffff,0x200,0x10);
        printk("ICLKEN_CORE %x\n", omap2_cm_read_mod_reg(0x200,0x10));


        printk("FCLKEN_CORE %x\n", omap2_cm_read_mod_reg(0x200,0x00));
        omap2_cm_write_mod_reg(0xffffffff,0x200,0x00);
        printk("FCLKEN_CORE %x\n", omap2_cm_read_mod_reg(0x200,0x00));

        printk("CTRL i2c 1 %x\n", omap_ctrl_readl(0x1BC));
        printk("CTRL i2c 2 %x\n", omap_ctrl_readl(0x1C0));
        printk("CTRL i2c 3 %x\n", omap_ctrl_readl(0x1C4));


	/* Ensure SDRC pins are mux'd for self-refresh */
	//omap_mux_init_signal("sdrc_cke0", OMAP_PIN_OUTPUT);
	//omap_mux_init_signal("sdrc_cke1", OMAP_PIN_OUTPUT);
}
