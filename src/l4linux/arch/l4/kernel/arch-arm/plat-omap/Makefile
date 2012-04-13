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


# Common support
obj-y :=
#obj-y := common.o sram.o clock.o devices.o dma.o mux.o \
#	 usb.o fb.o io.o counter_32k.o
obj-m :=
obj-n :=
obj-  :=

ifeq ($(CONFIG_L4_PLAT_OVERO),y)

#obj-y += sram.o
obj-y += clock.o dma.o

obj-y += omap_device.o
#obj-y += mcbsp.o

obj-y += i2c.o
#obj-y += omap-pm-noop.o

endif