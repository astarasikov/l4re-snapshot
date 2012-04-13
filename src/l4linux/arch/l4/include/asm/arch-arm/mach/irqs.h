#ifndef __ASM_L4__ARCH_ARM__ARCH__IRQS_H__
#define __ASM_L4__ARCH_ARM__ARCH__IRQS_H__

#ifdef  CONFIG_L4_PLAT_NONE
#define NR_IRQS		220
#define NR_IRQS_HW	210
#endif

#ifdef  CONFIG_L4_PLAT_OVERO
#define NR_IRQS		410
#define NR_IRQS_HW	385
#endif

#endif /* ! __ASM_L4__ARCH_ARM__ARCH__IRQS_H__ */
