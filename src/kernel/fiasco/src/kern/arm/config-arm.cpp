/* ARM specific */
INTERFACE [arm]:

EXTENSION class Config
{
public:

  enum
  {
    Access_user_mem = Must_access_user_mem_direct,

    PAGE_SHIFT = ARCH_PAGE_SHIFT,
    PAGE_SIZE  = 1 << PAGE_SHIFT,
    PAGE_MASK  = ~(PAGE_SIZE - 1),

    SUPERPAGE_SHIFT = 20,
    SUPERPAGE_SIZE  = 1 << SUPERPAGE_SHIFT,
    SUPERPAGE_MASK  = ~(SUPERPAGE_SIZE -1),

    hlt_works_ok = 1,
    Irq_shortcut = 1,
  };

  enum
  {
#ifdef CONFIG_ONE_SHOT
    scheduler_one_shot		= 1,
    scheduler_granularity	= 1UL,
    default_time_slice	        = 10000 * scheduler_granularity,
#else
    scheduler_one_shot		= 0,
    scheduler_granularity	= 1000UL,
    default_time_slice	        = 10 * scheduler_granularity,
#endif
  };

  enum
  {
    KMEM_SIZE = 16 << 20,
  };

  // the default uart to use for serial console
  static unsigned const default_console_uart	= 3;
  static unsigned const default_console_uart_baudrate = 115200;

  static const bool getchar_does_hlt = false;
  static const bool getchar_does_hlt_works_ok = true;
  static const char char_micro;
  static const bool enable_io_protection = false;

#ifdef CONFIG_VMEM_ALLOC_TEST
  static bool const VMEM_ALLOC_TEST = true;
#else
  static bool const VMEM_ALLOC_TEST = false;
#endif

  static const bool cache_enabled = true;

  enum
  {
#ifdef CONFIG_ARM_CA9_ENABLE_SWP
    Cp15_c1_use_a9_swp_enable = 1,
#else
    Cp15_c1_use_a9_swp_enable = 0,
#endif
#ifdef CONFIG_ARM_ALIGNMENT_CHECK
    Cp15_c1_use_alignment_check = 1,
#else
    Cp15_c1_use_alignment_check = 0,
#endif

    Support_arm_linux_cache_API = 1,
  };

};

//---------------------------------------------------------------------------
IMPLEMENTATION [arm]:

char const Config::char_micro = '\265';
const char *const Config::kernel_warn_config_string = 0;

IMPLEMENT FIASCO_INIT
void
Config::init_arch()
{}

//---------------------------------------------------------------------------
IMPLEMENTATION [armv6plus]:

#include "feature.h"

KIP_KERNEL_FEATURE("armv6plus");
