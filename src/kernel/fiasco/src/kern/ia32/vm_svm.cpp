INTERFACE [svm]:

#include "config.h"
#include "vm.h"

class Vmcb;

class Vm_svm : public Vm
{
private:
  static void resume_vm_svm(Mword phys_vmcb, Vcpu_state *regs)
    asm("resume_vm_svm") __attribute__((__regparm__(3)));
  Unsigned8 _asid[Config::Max_num_cpus];
  Unsigned32 _asid_generation[Config::Max_num_cpus];

  enum
  {
    EFER_LME = 1 << 8,
    EFER_LMA = 1 << 10,
  };
};

// ------------------------------------------------------------------------
INTERFACE [svm && debug]:

EXTENSION class Vm_svm
{
protected:
  struct Log_vm_svm_exit
  {
    Mword exitcode, exitinfo1, exitinfo2, rip;
  };

  static unsigned log_fmt_svm(Tb_entry *, int max, char *buf) asm ("__fmt_vm_svm_exit");
};

// ------------------------------------------------------------------------
IMPLEMENTATION [svm]:

#include "context.h"
#include "mem_space.h"
#include "fpu.h"
#include "ref_ptr.h"
#include "svm.h"
#include "thread.h" // XXX: circular dep, move this out here!
#include "thread_state.h" // XXX: circular dep, move this out here!


// ------------------------------------------------------------------------
IMPLEMENTATION [svm && ia32]:

#include "virt.h"

PRIVATE inline NEEDS["virt.h"]
Address
Vm_svm::get_vm_cr3(Vmcb *)
{
  // When running in 32bit mode we already return the page-table of our Vm
  // object, whether we're running with shadow or nested paging
  return mem_space()->phys_dir();
}

//----------------------------------------------------------------------------
IMPLEMENTATION [svm && amd64]:

#include "virt.h"

PRIVATE inline NEEDS["virt.h"]
Address
Vm_svm::get_vm_cr3(Vmcb *v)
{
  // When we have nested paging, we just return the 4lvl host page-table of
  // our Vm.
  if (v->np_enabled())
     return mem_space()->phys_dir();

  // When running with shadow paging and the guest is running in long mode
  // and has paging enabled, we can just return the 4lvl page table of our
  // host Vm object.
  if (   (v->state_save_area.efer & EFER_LME)
      && (v->state_save_area.cr0 & CR0_PG))
    return mem_space()->phys_dir();

  // Now it's getting tricky when running with shadow paging.
  // We need to obey the following rules:
  //  - When the guest is not running in 64bit mode the CR3 one can set for
  //    the page-table must be below 4G physical memory (i.e. bit 32-63 must
  //    be zero). This is unfortunate when the host has memory above 4G as
  //    Fiasco gets its memory from the end of physical memory, i.e.
  //    page-table memory is above 4G.
  //  - We need an appropriate page-table format for 32bit!
  //    That means either a 2lvl page-table or a 3lvl PAE one. That would
  //    require to maintain two page-tables for the guest, one for 32bit
  //    mode execution and one for 64 bit execution. It is needed either for
  //    the transition from real to long-mode via protected mode or for
  //    32bit only guests.
  //    There's one trick to avoid having two PTs: 4lvl-PTs and 3lvl-PAE-PTs
  //    have much in common so that it's possible to just take the the PDPE
  //    one of the host as the 3lvl-PAE-PT for the guest. Well, not quite.
  //    The problem is that SVM checks that MBZ bits in the PAE-PT entries
  //    are really 0 as written in the spec. Now the 4lvl PT contains rights
  //    bits there, so that this type of PT is refused and does not work on
  //    real hardware.
  //    So why is the code still here? Well, QEmu isn't so picky about the
  //    bits in the PDPE and it thus works there...
  Address vm_cr3 = mem_space()->dir()->walk(Virt_addr(0), 0).e->addr();
  if (EXPECT_FALSE(!vm_cr3))
    {
      // force allocation of new secondary page-table level
      mem_space()->dir()->alloc_cast<Mem_space_q_alloc>()
                 ->walk(Virt_addr(0), 1, Mem_space_q_alloc(ram_quota(),
                        Mapped_allocator::allocator()));
      vm_cr3 = mem_space()->dir()->walk(Virt_addr(0), 0).e->addr();
    }

  if (EXPECT_FALSE(vm_cr3 >= 1UL << 32))
    {
      WARN("svm: Host page-table not under 4G, sorry.\n");
      return 0;
    }

  return vm_cr3;
}

//----------------------------------------------------------------------------
IMPLEMENTATION [svm]:

PRIVATE inline
Unsigned8
Vm_svm::asid ()
{
  return _asid[current_cpu()];
}

PRIVATE inline
void
Vm_svm::asid(Unsigned8 asid)
{
  _asid[current_cpu()] = asid;
}

PRIVATE inline
Unsigned32
Vm_svm::asid_generation()
{
  return _asid_generation[current_cpu()];
}

PRIVATE inline
void
Vm_svm::asid_generation(Unsigned32 generation)
{
  _asid_generation[current_cpu()] = generation;
}

PUBLIC
Vm_svm::Vm_svm(Ram_quota *q)
  : Vm(q)
{
  memset(_asid, 0, sizeof(_asid));
  memset(_asid_generation, 0, sizeof(_asid_generation));
}

PUBLIC inline
void *
Vm_svm::operator new (size_t size, void *p)
{
  (void)size;
  assert (size == sizeof (Vm_svm));
  return p;
}

PUBLIC
void
Vm_svm::operator delete (void *ptr)
{
  Vm_svm *t = reinterpret_cast<Vm_svm*>(ptr);
  allocator<Vm_svm>()->q_free(t->ram_quota(), ptr);
}


// to do:
//   - handle cr2
//   - force fpu ownership
//   - debug registers not covered by VMCB

PRIVATE
void
Vm_svm::copy_state_save_area(Vmcb *dest, Vmcb *src)
{
  Vmcb_state_save_area *d = &dest->state_save_area;
  Vmcb_state_save_area *s = &src->state_save_area;

  d->es_sel       = s->es_sel;
  d->es_attrib    = s->es_attrib;
  d->es_limit     = s->es_limit;
  d->es_base      = s->es_base;

  d->cs_sel       = s->cs_sel;
  d->cs_attrib    = s->cs_attrib;
  d->cs_limit     = s->cs_limit;
  d->cs_base      = s->cs_base;

  d->ss_sel       = s->ss_sel;
  d->ss_attrib    = s->ss_attrib;
  d->ss_limit     = s->ss_limit;
  d->ss_base      = s->ss_base;

  d->ds_sel       = s->ds_sel;
  d->ds_attrib    = s->ds_attrib;
  d->ds_limit     = s->ds_limit;
  d->ds_base      = s->ds_base;

  d->fs_sel       = s->fs_sel;
  d->fs_attrib    = s->fs_attrib;
  d->fs_limit     = s->fs_limit;
  d->fs_base      = s->fs_base;

  d->gs_sel       = s->gs_sel;
  d->gs_attrib    = s->gs_attrib;
  d->gs_limit     = s->gs_limit;
  d->gs_base      = s->gs_base;

  d->gdtr_sel     = s->gdtr_sel;
  d->gdtr_attrib  = s->gdtr_attrib;
  d->gdtr_limit   = s->gdtr_limit;
  d->gdtr_base    = s->gdtr_base;

  d->ldtr_sel     = s->ldtr_sel;
  d->ldtr_attrib  = s->ldtr_attrib;
  d->ldtr_limit   = s->ldtr_limit;
  d->ldtr_base    = s->ldtr_base;

  d->idtr_sel     = s->idtr_sel;
  d->idtr_attrib  = s->idtr_attrib;
  d->idtr_limit   = s->idtr_limit;
  d->idtr_base    = s->idtr_base;

  d->tr_sel       = s->tr_sel;
  d->tr_attrib    = s->tr_attrib;
  d->tr_limit     = s->tr_limit;
  d->tr_base      = s->tr_base;

  d->cpl          = s->cpl;
  d->efer         = s->efer;

  d->cr4          = s->cr4;
  d->cr3          = s->cr3;
  d->cr0          = s->cr0;
  d->dr7          = s->dr7;
  d->dr6          = s->dr6;
  d->rflags       = s->rflags;

  d->rip          = s->rip;
  d->rsp          = s->rsp;
  d->rax          = s->rax;

  d->star         = s->star;
  d->lstar        = s->lstar;
  d->cstar        = s->cstar;
  d->sfmask       = s->sfmask;
  d->kernelgsbase = s->kernelgsbase;
  d->sysenter_cs  = s->sysenter_cs;
  d->sysenter_esp = s->sysenter_esp;
  d->sysenter_eip = s->sysenter_eip;
  d->cr2          = s->cr2;

  d->g_pat        = s->g_pat;
  d->dbgctl       = s->dbgctl;
  d->br_from      = s->br_from;
  d->br_to        = s->br_to;
  d->lastexcpfrom = s->lastexcpfrom;
  d->last_excpto  = s->last_excpto;
}


PRIVATE
void
Vm_svm::copy_control_area(Vmcb *dest, Vmcb *src)
{
  Vmcb_control_area *d = &dest->control_area;
  Vmcb_control_area *s = &src->control_area;

  d->intercept_rd_crX          = s->intercept_rd_crX;
  d->intercept_wr_crX          = s->intercept_wr_crX;

  d->intercept_rd_drX          = s->intercept_rd_drX;
  d->intercept_wr_drX          = s->intercept_wr_drX;

  d->intercept_exceptions      = s->intercept_exceptions;

  d->intercept_instruction0    = s->intercept_instruction0;
  d->intercept_instruction1    = s->intercept_instruction1;

  // skip iopm_base_pa and msrpm_base_pa

  d->tsc_offset                = s->tsc_offset;
  d->guest_asid_tlb_ctl        = s->guest_asid_tlb_ctl;
  d->interrupt_ctl             = s->interrupt_ctl;
  d->interrupt_shadow          = s->interrupt_shadow;
  d->exitcode                  = s->exitcode;
  d->exitinfo1                 = s->exitinfo1;
  d->exitinfo2                 = s->exitinfo2;
  d->exitintinfo               = s->exitintinfo;
  d->np_enable                 = s->np_enable;

  d->eventinj                  = s->eventinj;
  d->n_cr3                     = s->n_cr3;
  d->lbr_virtualization_enable = s->lbr_virtualization_enable;
}


/* skip anything that does not change */
PRIVATE
void
Vm_svm::copy_control_area_back(Vmcb *dest, Vmcb *src)
{
  Vmcb_control_area *d = &dest->control_area;
  Vmcb_control_area *s = &src->control_area;

  d->interrupt_ctl    = s->interrupt_ctl;
  d->interrupt_shadow = s->interrupt_shadow;

  d->exitcode         = s->exitcode;
  d->exitinfo1        = s->exitinfo1;
  d->exitinfo2        = s->exitinfo2;
  d->exitintinfo      = s->exitintinfo;

  d->eventinj = s->eventinj;
}

/** \brief Choose an ASID for this Vm.
 *
 * Choose an ASID for this Vm. The ASID provided by userspace is ignored
 * instead the kernel picks one.
 * Userspace uses the flush-bit to receive a new ASID for this Vm.
 * All ASIDs are flushed as soon as the kernel runs out of ASIDs.
 *
 * @param vmcb_s external VMCB provided by userspace
 * @param kernel_vmcb_s our VMCB
 *
 */
PRIVATE
void
Vm_svm::configure_asid(Vmcb *vmcb_s, Vmcb *kernel_vmcb_s)
{
  assert (cpu_lock.test());

  Svm &s = Svm::cpus.cpu(current_cpu());

  if (// vmm requests flush
      ((vmcb_s->control_area.guest_asid_tlb_ctl >> 32) & 1) == 1 ||
      // our asid is not valid or expired
      !(s.asid_valid(asid(), asid_generation())))
    {
      asid(s.next_asid());
      asid_generation(s.global_asid_generation());
    }

  assert(s.asid_valid(asid(), asid_generation()));
#if 1
  kernel_vmcb_s->control_area.guest_asid_tlb_ctl = asid();
  if (s.flush_all_asids())
    {
      kernel_vmcb_s->control_area.guest_asid_tlb_ctl |= (1ULL << 32);
      s.flush_all_asids(false);
    }
#else
  kernel_vmcb_s->control_area.guest_asid_tlb_ctl = 1;
  kernel_vmcb_s->control_area.guest_asid_tlb_ctl |= (1ULL << 32);
#endif
}

PRIVATE inline NOEXPORT
int
Vm_svm::do_resume_vcpu(Context *ctxt, Vcpu_state *vcpu, Vmcb *vmcb_s)
{
  //Mword host_cr0;
  Unsigned64 orig_cr3, orig_ncr3;

  assert (cpu_lock.test());

  /* these 4 must not use ldt entries */
  assert (!(Cpu::get_cs() & (1 << 2)));
  assert (!(Cpu::get_ss() & (1 << 2)));
  assert (!(Cpu::get_ds() & (1 << 2)));
  assert (!(Cpu::get_es() & (1 << 2)));

  Svm &s = Svm::cpus.cpu(current_cpu());

  // FIXME: this can be an assertion I think, however, think about MP
  if (EXPECT_FALSE(!s.svm_enabled()))
    {
      WARN("svm: not supported/enabled\n");
      return -L4_err::EInval;
    }

  if (EXPECT_FALSE(vmcb_s->np_enabled() && !s.has_npt()))
    {
      WARN("svm: No NPT available\n");
      return -L4_err::EInval;
    }

  Address vm_cr3 = get_vm_cr3(vmcb_s);
  // can only fail on 64bit, will be optimized away on 32bit
  if (EXPECT_FALSE(is_64bit() && !vm_cr3))
    return -L4_err::ENomem;

  // neither EFER.LME nor EFER.LMA must be set
  if (EXPECT_FALSE(!is_64bit()
	           && (vmcb_s->state_save_area.efer & (EFER_LME | EFER_LMA))))
    {
      WARN("svm: EFER invalid %llx\n", vmcb_s->state_save_area.efer);
      return -L4_err::EInval;
    }

  // EFER.SVME must be set
  if (!(vmcb_s->state_save_area.efer & 0x1000))
    {
      WARN("svm: EFER invalid %llx\n", vmcb_s->state_save_area.efer);
      return -L4_err::EInval;
    }
  // allow PAE in combination with NPT
#if 0
  // CR4.PAE must be clear
  if(vmcb_s->state_save_area.cr4 & 0x20)
    return -L4_err::EInval;
#endif

  // XXX:
  // This generates a circular dep between thread<->task, this cries for a
  // new abstraction...
  if (!(ctxt->state() & Thread_fpu_owner))
    {
      if (!static_cast<Thread*>(ctxt)->switchin_fpu())
        {
          WARN("svm: switchin_fpu failed\n");
          return -L4_err::EInval;
        }
    }

#if 0  //should never happen
  host_cr0 = Cpu::get_cr0();
  // the VMM does not currently own the fpu but wants to
  // make it available for the guest. This may happen
  // if it was descheduled between activating the fpu and
  // executing the vm_run operation
  if (!(vmcb_s->state_save_area.cr0 & 0x8) && (host_cr0 & 0x8))
    {
      WARN("svm: FPU TS\n");
      return commit_result(-L4_err::EInval);
    }
#endif

  // increment our refcount, and drop it at the end automatically
  Ref_ptr<Vm_svm> pin_myself(this);

  // sanitize VMCB

  orig_cr3  = vmcb_s->state_save_area.cr3;
  orig_ncr3 = vmcb_s->control_area.n_cr3;

  Vmcb *kernel_vmcb_s = s.kernel_vmcb();

  copy_control_area(kernel_vmcb_s, vmcb_s);
  copy_state_save_area(kernel_vmcb_s, vmcb_s);

  if (EXPECT_FALSE(is_64bit() && !kernel_vmcb_s->np_enabled()
                   && (kernel_vmcb_s->state_save_area.cr0 & CR0_PG)
                   && !(kernel_vmcb_s->state_save_area.cr4 & CR4_PAE)))
    {
      WARN("svm: No 32bit shadow page-tables on AMD64, use PAE!\n");
      return -L4_err::EInval;
    }

  // set MCE according to host
  kernel_vmcb_s->state_save_area.cr4 |= Cpu::get_cr4() & CR4_MCE;

  // allow w access to cr0, cr2, cr3
  // allow r access to cr0, cr2, cr3, cr4
  // to do: check if enabling PAE in cr4 needs to be controlled

  // allow r/w access to dr[0-7]
  kernel_vmcb_s->control_area.intercept_rd_drX |= 0xff00;
  kernel_vmcb_s->control_area.intercept_wr_drX |= 0xff00;

#if 0
  // intercept exception vectors 0-31
  kernel_vmcb_s->control_area.intercept_exceptions = 0xffffffff;
#endif

  // enable iopm and msrpm
  kernel_vmcb_s->control_area.intercept_instruction0 |= 0x18000000;
  // intercept FERR_FREEZE and shutdown events
  kernel_vmcb_s->control_area.intercept_instruction0 |= 0xc0000000;
  // intercept INTR/NMI/SMI/INIT
  kernel_vmcb_s->control_area.intercept_instruction0 |= 0xf;
  // intercept INVD
  kernel_vmcb_s->control_area.intercept_instruction0 |= (1 << 22);
  // intercept HLT
  kernel_vmcb_s->control_area.intercept_instruction0 |= (1 << 24);
  // intercept task switch
  kernel_vmcb_s->control_area.intercept_instruction0 |= (1 << 29);
  // intercept shutdown
  kernel_vmcb_s->control_area.intercept_instruction0 |= (1 << 31);
  // intercept MONITOR/MWAIT
  kernel_vmcb_s->control_area.intercept_instruction1 |= (1 << 10) | (1 << 11);

  // intercept virtualization related instructions
  //  vmrun interception is required by the hardware
  kernel_vmcb_s->control_area.intercept_instruction1 |= 0xff;

  Mword kernel_vmcb_pa = s.kernel_vmcb_pa();
  Unsigned64 iopm_base_pa = s.iopm_base_pa();
  Unsigned64 msrpm_base_pa = s.msrpm_base_pa();

  kernel_vmcb_s->control_area.iopm_base_pa = iopm_base_pa;
  kernel_vmcb_s->control_area.msrpm_base_pa = msrpm_base_pa;

  configure_asid(vmcb_s, kernel_vmcb_s);

  // 7:0 V_TPR, 8 V_IRQ, 15:9 reserved SBZ,
  // 19:16 V_INTR_PRIO, 20 V_IGN_TPR, 23:21 reserved SBZ
  // 24 V_INTR_MASKING 31:25 reserved SBZ
  // 39:32 V_INTR_VECTOR, 63:40 reserved SBZ
#if 0
  kernel_vmcb_s->control_area.interrupt_ctl = 0x10f0000;
#endif
  // enable IRQ masking virtualization
  kernel_vmcb_s->control_area.interrupt_ctl |= 0x01000000;

#if 0
  // 0 INTERRUPT_SHADOW, 31:1 reserved SBZ
  // 63:32 reserved SBZ
  kernel_vmcb_s->control_area.interrupt_shadow = 0;
#endif

  kernel_vmcb_s->control_area.exitcode = 0;
  kernel_vmcb_s->control_area.exitinfo1 = 0;
  kernel_vmcb_s->control_area.exitinfo2 = 0;
  kernel_vmcb_s->control_area.exitintinfo = 0;

#if 0
  // 0/1 NP_ENABLE, 31:1 reserved SBZ
  kernel_vmcb_s->control_area.np_enable = 1;

  // 31 VALID, EVENTINJ
  kernel_vmcb_s->control_area.eventinj = 0;
#endif

  // N_CR3
  kernel_vmcb_s->control_area.n_cr3 = vm_cr3;

  if (!kernel_vmcb_s->np_enabled())
    {
      // to do: check that the vmtask has the
      // VM property set, i.e. does not contain mappings
      // to the fiasco kernel regions or runs with PL 3

      // printf("nested paging disabled, use n_cr3 as cr3\n");
      kernel_vmcb_s->state_save_area.cr3 = vm_cr3;

      // intercept accesses to cr0, cr3 and cr4
      kernel_vmcb_s->control_area.intercept_rd_crX = 0xfff9;
      kernel_vmcb_s->control_area.intercept_wr_crX = 0xfff9;
    }

#if 0
  kernel_vmcb_s->control_area.lbr_virtualization_enable = 0;
#endif


  // to do:
  // - initialize VM_HSAVE_PA (done)
  // - supply trusted msrpm_base_pa and iopm_base_pa (done)
  // - save host state not covered by VMRUN/VMEXIT (ldt, some segments etc) (done)
  // - disable interupts (done)
  // - trigger interecepted device and timer interrupts (done, not necessary)
  // - check host CR0.TS (floating point registers) (done)

  Unsigned64 sysenter_cs, sysenter_eip, sysenter_esp;
  Unsigned32 fs, gs;
  Unsigned16 tr, ldtr;
  //Unsigned32 cr4;

  sysenter_cs = Cpu::rdmsr(MSR_SYSENTER_CS);
  sysenter_eip = Cpu::rdmsr(MSR_SYSENTER_EIP);
  sysenter_esp = Cpu::rdmsr(MSR_SYSENTER_ESP);

  fs = Cpu::get_fs();
  gs = Cpu::get_gs();
  tr = Cpu::get_tr();
  ldtr = Cpu::get_ldt();

  Gdt_entry tr_entry;

  tr_entry = (*Cpu::cpus.cpu(current_cpu()).get_gdt())[tr / 8];

#if 0
  // to do: check if the nested page table walker looks
  // into the TLB. if so, global pages have to be disabled in
  // the host
  cr4 = Cpu::get_cr4();

  if (cr4 & CR4_PGE)
    // disable support for global pages as the vm task has
    // a divergent upper memory region from the regular tasks
    Cpu::set_cr4(cr4 & ~CR4_PGE);
#endif

  resume_vm_svm(kernel_vmcb_pa, vcpu);


#if 0
  if (cr4 & CR4_PGE)
    Cpu::set_cr4(cr4);
#endif

  Cpu::wrmsr(sysenter_cs, MSR_SYSENTER_CS);
  Cpu::wrmsr(sysenter_eip, MSR_SYSENTER_EIP);
  Cpu::wrmsr(sysenter_esp, MSR_SYSENTER_ESP);

  Cpu::set_ldt(ldtr);
  Cpu::set_fs(fs);
  Cpu::set_gs(gs);

  // clear busy flag
  Gdt_entry tss_entry;

  tss_entry = (*Cpu::cpus.cpu(current_cpu()).get_gdt())[tr / 8];
  tss_entry.access &= 0xfd;
  (*Cpu::cpus.cpu(current_cpu()).get_gdt())[tr / 8] = tss_entry;

  Cpu::set_tr(tr); // TODO move under stgi in asm

  copy_state_save_area(vmcb_s, kernel_vmcb_s);
  copy_control_area_back(vmcb_s, kernel_vmcb_s);

  if (!(vmcb_s->np_enabled()))
    vmcb_s->state_save_area.cr3 = orig_cr3;

  vmcb_s->control_area.n_cr3 = orig_ncr3;

  LOG_TRACE("VM-SVM", "svm", current(), __fmt_vm_svm_exit,
            Log_vm_svm_exit *l = tbe->payload<Log_vm_svm_exit>();
            l->exitcode = vmcb_s->control_area.exitcode;
            l->exitinfo1 = vmcb_s->control_area.exitinfo1;
            l->exitinfo2 = vmcb_s->control_area.exitinfo2;
            l->rip       = vmcb_s->state_save_area.rip;
           );

  // check for IRQ exit
  if (kernel_vmcb_s->control_area.exitcode == 0x60)
    return 1;

  vcpu->state &= ~(Vcpu_state::F_traps | Vcpu_state::F_user_mode);
  return 0;
}

PUBLIC
int
Vm_svm::resume_vcpu(Context *ctxt, Vcpu_state *vcpu, bool user_mode)
{
  (void)user_mode;
  assert_kdb (user_mode);

  if (EXPECT_FALSE(!(ctxt->state(true) & Thread_ext_vcpu_enabled)))
    return -L4_err::EInval;

  Vmcb *vmcb_s = reinterpret_cast<Vmcb*>(reinterpret_cast<char *>(vcpu) + 0x400);
  for (;;)
    {
      // in the case of disabled IRQs and a pending IRQ directly simulate an
      // external interrupt intercept
      if (   !(vcpu->_saved_state & Vcpu_state::F_irqs)
	  && (vcpu->sticky_flags & Vcpu_state::Sf_irq_pending))
	{
	  vmcb_s->control_area.exitcode = 0x60;
	  return 1; // return 1 to indicate pending IRQs (IPCs)
	}

      int r = do_resume_vcpu(ctxt, vcpu, vmcb_s);

      // test for error or non-IRQ exit reason
      if (r <= 0)
	return r;

      // check for IRQ exits and allow to handle the IRQ
      if (r == 1)
	Proc::preemption_point();

      // Check if the current context got a message delivered.
      // This is done by testing for a valid continuation.
      // When a continuation is set we have to directly
      // leave the kernel to not overwrite the vcpu-regs
      // with bogus state.
      Thread *t = nonull_static_cast<Thread*>(ctxt);

      if (t->continuation_test_and_restore())
        t->fast_return_to_user(vcpu->_entry_ip, vcpu->_entry_sp,
                               t->vcpu_state().usr().get());
    }
}



// ------------------------------------------------------------------------
IMPLEMENTATION [svm && debug]:

IMPLEMENT
unsigned
Vm_svm::log_fmt_svm(Tb_entry *e, int max, char *buf)
{
  Log_vm_svm_exit *l = e->payload<Log_vm_svm_exit>();
  return snprintf(buf, max, "ec=%lx ei1=%08lx ei2=%08lx rip=%08lx",
                  l->exitcode, l->exitinfo1, l->exitinfo2, l->rip);
}
