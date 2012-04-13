#ifndef __ASM_MACH_CLKDEV_H
#define __ASM_MACH_CLKDEV_H

//#include <plat/clock.h>
#ifdef  CONFIG_L4_PLAT_NONE
struct clk {
	const struct clk_ops	*ops;
};
#endif

#define __clk_get(clk) ({ 1; })
#define __clk_put(clk) do { } while (0)

#endif
