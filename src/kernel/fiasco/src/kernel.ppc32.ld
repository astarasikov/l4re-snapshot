/* -*- c -*- */

/* TODO cbass: define in ppc32 Makerules.KERNEL */
/*kernel_load_addr = 0xfd000000;*/	/* kernel link address */

ENTRY(_start)

PHDRS {
  etext PT_LOAD;
  etextcommon PT_LOAD;
  phys PT_LOAD;
  kip PT_LOAD;
  koptions PT_LOAD;
  ktext PT_LOAD;
  kdata PT_LOAD;
  kitext PT_LOAD;
  kidata PT_LOAD;
}

SECTIONS {
  /* 0x0 */
  .except : AT (0x0) {
     *(.except)
  } : etext = 0x0
  . = ALIGN(4K) + 8K;

  /* 0x3000 */
  PROVIDE (_kernel_image_start = .);
  .physmem : AT(ADDR(.physmem)) {
  } : phys = 0x0

  .kip : AT(ADDR(.kip)) {
    *(.kernel_info_page)
    _initkip_start = .;
    KEEP(*(.initkip.version))
    KEEP(*(.initkip.features))
    KEEP(*(.initkip.features.fini))
    _initkip_end = .;
    . = ALIGN(4K);
  } : kip = 0

  .koptions : AT (ADDR(.koptions)) {
    *(.koptions)
  } : koptions = 0

  .mp_tramp : AT (ADDR(.mp_tramp)) {
      KEEP(*(.mp_tramp))
  }

  .exceptcommon : AT(ADDR(.exceptcommon)) {
    *(.exceptcommon)
  } : etextcommon = 0x0

  .text : AT (ADDR(.text)) {
    crt0.o(.text)
    *(.init)
    *(.text SORT(.text.*) .gnu.linkonce.t.*)
    *(.fini)

    PROVIDE (_ecode = .);

    *(.rodata .rodata.* .gnu.linkonce.r.*)

    . = ALIGN(4);
    PROVIDE (_log_table = .);
    /*
     * The section must start with ".debug" because else the linker
     * complains about references to discarded sections
      */
    KEEP(*(SORT(.debug.jdb.log_table)))
    PROVIDE (_log_table_end = .);

    . = ALIGN(0x40);

    PROVIDE (_etext = .);
  } : ktext = 0x0000000

  . = ALIGN(0x10);
  .data : AT (ADDR(.data)) {
    *(.data .data.* .gnu.linkonce.d.*)
    *(.anno)

    /* Constructor and destructor lists, ordered by priority.  The lists
       are traversed from end to start, therefore the *_END__ symbols
       precede the *_LIST__ symbols. */
    . = ALIGN(8);

    __PER_CPU_CTORS_END__ = .;
    KEEP (*(.ctors.00010))
    KEEP (*(.ctors.00009))
    KEEP (*(.ctors.00008))
    KEEP (*(.ctors.00007))
    KEEP (*(.ctors.00006))
    KEEP (*(.ctors.00005))
    KEEP (*(.ctors.00004))
    KEEP (*(.ctors.00003))
    KEEP (*(.ctors.00002))
    KEEP (*(.ctors.00001))
    __PER_CPU_CTORS_LIST__ = .;

    __PER_CPU_LATE_CTORS_END__ = .;
    KEEP (*(.ctors.00020))
    KEEP (*(.ctors.00019))
    KEEP (*(.ctors.00018))
    KEEP (*(.ctors.00017))
    KEEP (*(.ctors.00016))
    KEEP (*(.ctors.00015))
    KEEP (*(.ctors.00014))
    KEEP (*(.ctors.00013))
    KEEP (*(.ctors.00012))
    KEEP (*(.ctors.00011))
    __PER_CPU_LATE_CTORS_LIST__ = .;

    __CTOR_END__ = .;
    CONSTRUCTORS
    KEEP (*(.ctors))
    KEEP (*(SORT (.ctors.*)))
    __CTOR_LIST__ = .;

    __DTOR_END__ = .;    
    KEEP (*(SORT (.dtors.*)))
    KEEP (*(.dtors))
    __DTOR_LIST__ = .;

    PROVIDE (_edata = .);
  } : kdata

  . = ALIGN(8);
  .per_cpu_data : AT (ADDR(.per_cpu_data)) {
    PROVIDE (_per_cpu_data_start = .);
    *(.per_cpu.data)
    . = ALIGN(8);
    PROVIDE (_per_cpu_data_end = .);
  } : kdata

  . = ALIGN(0x10);
  .bss : AT (ADDR(.bss)) {
    PROVIDE (_bss_start = .);
    *(.bss .bss.* .gnu.linkonce.b.* COMMON)
    PROVIDE (_bss_end = .);
  } : kdata

  . = ALIGN(4K);
  PROVIDE (_end = .);

  .initcall.text : AT (ADDR(.initcall.text)) {
  PROVIDE (_initcall_start = .);
    *(.initcall.text*)
  } : kitext = 0x000000000

  .initcall.data : AT (ADDR(.initcall.data)) {
    *(.initcall.data*)
    . = ALIGN(4K);
    PROVIDE (_initcall_end = .);
  } : kidata

  /DISCARD/ : {
    *(.note)
    *(.note.ABI-tag)
    *(.comment)
    *(.eh_frame)
    *(.dummy)
  }
}
