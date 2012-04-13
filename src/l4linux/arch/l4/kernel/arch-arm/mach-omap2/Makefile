#
# Makefile for the linux kernel.
#

KBUILD_CPPFLAGS += -Iarch/l4/kernel/arch-arm/plat-omap/include

KBUILD_CPPFLAGS	+= -DCONFIG_ARCH_OMAP3
KBUILD_CPPFLAGS	+= -DCONFIG_ARCH_OMAP2PLUS
KBUILD_CPPFLAGS	+= -DCONFIG_SOC_OMAP3430
KBUILD_CPPFLAGS	+= -DCONFIG_ARCH_OMAP_OTG
KBUILD_CPPFLAGS	+= -DCONFIG_I2C_OMAP
KBUILD_CPPFLAGS	+= -DCONFIG_USB_MUSB_OMAP2PLUS
KBUILD_CPPFLAGS	+= -DCONFIG_USB_MUSB_HDRC
KBUILD_CPPFLAGS	+= -DCONFIG_USB_MUSB_SOC
KBUILD_CPPFLAGS	+= -DCONFIG_PM_OPP
#KBUILD_CPPFLAGS	+= -DCONFIG_OMAP_MUX
#KBUILD_CPPFLAGS	+= -DCONFIG_OMAP_PACKAGE_CBB

# Common support
#obj-y := id.o devices.o serial.o gpmc.o timer-gp.o \
#	 common.o gpio.o 

#omap-2-3-common				= irq.o sdrc.o
hwmod-common				= omap_hwmod.o \
					  omap_hwmod_common_data.o
clock-common				= clock.o clock_common_data.o \
					  clkt_dpll.o clkt_clksel.o

ifeq ($(CONFIG_L4_PLAT_OVERO),y)

obj-y += io.o control.o irq.o mux.o dma.o wd_timer.o

obj-y += sdrc.o

obj-y += $(hwmod-common)
obj-y += omap_hwmod_3xxx_data.o

#obj-y += mcbsp.o

obj-y += omap_twl.o

obj-y += mux34xx.o


# OPP table initialization
obj-y		+= opp.o
obj-y		+= opp3xxx_data.o

#obj-y		+= sleep24xx.o
#obj-y		+= pm34xx.o sleep34xx.o cpuidle34xx.o
obj-y		+= pm.o 

# PRCM
obj-y		+= prcm.o cm2xxx_3xxx.o prm2xxx_3xxx.o \
					   vc3xxx_data.o vp3xxx_data.o

# OMAP voltage domains
voltagedomain-common			:= voltage.o
obj-y		+= $(voltagedomain-common) \
					   voltagedomains3xxx_data.o

# OMAP powerdomain framework
powerdomain-common			+= powerdomain.o powerdomain-common.o
obj-y		+= $(powerdomain-common) \
					   powerdomain2xxx_3xxx.o \
					   powerdomains3xxx_data.o \
					   powerdomains2xxx_3xxx_data.o

# PRCM clockdomain control
obj-y		+= clockdomain.o \
					   clockdomain2xxx_3xxx.o \
					   clockdomains2xxx_3xxx_data.o

# Clock framework
obj-y		+= $(clock-common) clock3xxx.o \
					   clock34xx.o clkt34xx_dpll3m2.o \
					   clock3517.o clock36xx.o \
					   dpll3xxx.o clock3xxx_data.o \
					   clkt_iclk.o

i2c-omap-y		:= i2c.o
obj-y	+= $(i2c-omap-m) $(i2c-omap-y)


obj-y	+= board-overo.o 

# Platform specific device init code
#usbfs-y		:= usb-fs.o
#obj-y					+= $(usbfs-m) $(usbfs-y)
obj-y					+= usb-musb.o

obj-y					+= common-board-devices.o

endif

