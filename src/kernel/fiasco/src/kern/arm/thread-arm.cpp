INTERFACE [arm]:

class Trap_state;

EXTENSION class Thread
{
private:
  bool _in_exception;

};

// ------------------------------------------------------------------------
IMPLEMENTATION [arm]:

#include <cassert>
#include <cstdio>

#include "globals.h"
#include "kmem_space.h"
#include "mem_op.h"
#include "static_assert.h"
#include "thread_state.h"
#include "types.h"
#include "vmem_alloc.h"

enum {
  FSR_STATUS_MASK = 0x0d,
  FSR_TRANSL      = 0x05,
  FSR_DOMAIN      = 0x09,
  FSR_PERMISSION  = 0x0d,
};

Per_cpu<Thread::Dbg_stack> DEFINE_PER_CPU Thread::dbg_stack;

PRIVATE static
void
Thread::print_page_fault_error(Mword e)
{
  char const *const excpts[] =
    { "reset","undef. insn", "swi", "pref. abort", "data abort",
      "XXX", "XXX", "XXX" };

  unsigned ex = (e >> 20) & 0x07;

  printf("(%lx) %s, %s(%c)",e & 0xff, excpts[ex],
         (e & 0x00010000)?"user":"kernel",
         (e & 0x00020000)?'r':'w');
}

PUBLIC inline
void FIASCO_NORETURN
Thread::fast_return_to_user(Mword ip, Mword sp, Vcpu_state *arg)
{
  extern char __iret[];
  assert_kdb((regs()->psr & Proc::Status_mode_mask) == Proc::Status_mode_user);

  regs()->ip(ip);
  regs()->sp(sp); // user-sp is in lazy user state and thus handled by
                  // fill_user_state()
  fill_user_state();

  regs()->psr &= ~Proc::Status_thumb;

    {
      register Vcpu_state *r0 asm("r0") = arg;

      asm volatile
	("mov sp, %0  \t\n"
	 "mov pc, %1  \t\n"
	 :
	 : "r" (nonull_static_cast<Return_frame*>(regs())), "r" (__iret), "r"(r0)
	);
    }
  panic("__builtin_trap()");
}

//
// Public services
//

IMPLEMENT
void
Thread::user_invoke()
{
  user_invoke_generic();
  assert (current()->state() & Thread_ready);

  Trap_state *ts = nonull_static_cast<Trap_state*>
    (nonull_static_cast<Return_frame*>(current()->regs()));

  static_assert(sizeof(ts->r[0]) == sizeof(Mword), "Size mismatch");
  Mem::memset_mwords(&ts->r[0], 0, sizeof(ts->r) / sizeof(ts->r[0]));

  if (current()->space() == sigma0_task)
    ts->r[0] = Kmem_space::kdir()->walk(Kip::k(), 0, false, 0).phys(Kip::k());

  extern char __return_from_exception;

  asm volatile
    ("  mov sp, %[stack_p]    \n"    // set stack pointer to regs structure
     "  mov pc, %[rfe]        \n"
     :
     :
     [stack_p] "r" (ts),
     [rfe]     "r" (&__return_from_exception)
     );

  panic("should never be reached");
  while (1)
    {
      current()->state_del(Thread_ready);
      current()->schedule();
    };

  // never returns here
}

IMPLEMENT inline NEEDS["space.h", <cstdio>, "types.h" ,"config.h"]
bool Thread::handle_sigma0_page_fault( Address pfa )
{
  return (mem_space()->v_insert(
	Mem_space::Phys_addr::create((pfa & Config::SUPERPAGE_MASK)),
	Mem_space::Addr::create(pfa & Config::SUPERPAGE_MASK),
	Mem_space::Size(Config::SUPERPAGE_SIZE),
	Mem_space::Page_writable | Mem_space::Page_user_accessible
	| Mem_space::Page_cacheable)
      != Mem_space::Insert_err_nomem);
}

typedef bool (*Undef_coprop_insn_handler)(Unsigned32 opcode, Trap_state *ts);
static Undef_coprop_insn_handler handle_copro_fault[16];

extern "C" {

  /**
   * The low-level page fault handler called from entry.S.  We're invoked with
   * interrupts turned off.  Apart from turning on interrupts in almost
   * all cases (except for kernel page faults in TCB area), just forwards
   * the call to Thread::handle_page_fault().
   * @param pfa page-fault virtual address
   * @param error_code CPU error code
   * @return true if page fault could be resolved, false otherwise
   */
  Mword pagefault_entry(const Mword pfa, Mword error_code,
                        const Mword pc, Return_frame *ret_frame)
  {
#if 0 // Double PF detect
    static unsigned long last_pfa = ~0UL;
    LOG_MSG_3VAL(current(),"PF", pfa, error_code, pc);
    if (last_pfa == pfa || pfa == 0)
      kdb_ke("DBF");
    last_pfa = pfa;
#endif
    if (EXPECT_FALSE(PF::is_alignment_error(error_code)))
      {
	printf("KERNEL%d: alignment error at %08lx (PC: %08lx, SP: %08lx, FSR: %lx, PSR: %lx)\n",
               current_cpu(), pfa, pc, ret_frame->usp, error_code, ret_frame->psr);
        return false;
      }

    Thread *t = current_thread();

    // Pagefault in user mode
    if (PF::is_usermode_error(error_code))
      {
	if (t->vcpu_pagefault(pfa, error_code, pc))
	  return 1;
	t->state_del(Thread_cancel);
        Proc::sti();
      }
    // or interrupts were enabled
    else if (!(ret_frame->psr & Proc::Status_IRQ_disabled))
      Proc::sti();

      // Pagefault in kernel mode and interrupts were disabled
    else
      {
	// page fault in kernel memory region, not present, but mapping exists
	if (Kmem::is_kmem_page_fault (pfa, error_code))
	  {
	    // We've interrupted a context in the kernel with disabled interrupts,
	    // the page fault address is in the kernel region, the error code is
	    // "not mapped" (as opposed to "access error"), and the region is
	    // actually valid (that is, mapped in Kmem's shared page directory,
	    // just not in the currently active page directory)
	    // Remain cli'd !!!
	  }
	else if (!Config::conservative &&
	    !Kmem::is_kmem_page_fault (pfa, error_code))
	  {
	    // No error -- just enable interrupts.
	    Proc::sti();
	  }
	else
	  {
	    // Error: We interrupted a cli'd kernel context touching kernel space
	    if (!Thread::log_page_fault())
	      printf("*P[%lx,%lx,%lx] ", pfa, error_code, pc);

	    kdb_ke ("page fault in cli mode");
	  }

      }

    // cache operations we carry out for user space might cause PFs, we just
    // ignore those
    if (EXPECT_FALSE(t->is_ignore_mem_op_in_progress()))
      {
        ret_frame->pc += 4;
        return 1;
      }

    // PFs in the kern_lib_page are always write PFs due to rollbacks and
    // insn decoding
    if (EXPECT_FALSE((pc & Kmem::Kern_lib_base) == Kmem::Kern_lib_base))
      error_code |= (1UL << 11);

    return t->handle_page_fault(pfa, error_code, pc, ret_frame);
  }

  void slowtrap_entry(Trap_state *ts)
  {
    Thread *t = current_thread();

    LOG_TRAP;

    if (Config::Support_arm_linux_cache_API)
      {
	if (   ts->error_code == 0x00200000
            && ts->r[7] == 0xf0002)
	  {
            if (ts->r[2] == 0)
              Mem_op::arm_mem_cache_maint(Mem_op::Op_cache_coherent,
                                          (void *)ts->r[0], (void *)ts->r[1]);
            return;
	  }
      }

    if (ts->exception_is_undef_insn())
      {
	Unsigned32 opcode;

	if (ts->psr & Proc::Status_thumb)
	  {
	    Unsigned16 v = *(Unsigned16 *)(ts->pc - 2);
	    if ((v >> 11) <= 0x1c)
	      goto undef_insn;

	    opcode = (v << 16) | *(Unsigned16 *)ts->pc;
	  }
	else
	  opcode = *(Unsigned32 *)(ts->pc - 4);

        if ((opcode & 0x0c000000) == 0x0c000000)
          {
            unsigned copro = (opcode >> 8) & 0xf;
            if (   handle_copro_fault[copro]
                && handle_copro_fault[copro](opcode, ts))
              return;
          }
      }

undef_insn:
    // send exception IPC if requested
    if (t->send_exception(ts))
      return;

    // exception handling failed
    if (Config::conservative)
      kdb_ke ("thread killed");

    t->halt();

  }

};

IMPLEMENT inline
bool
Thread::pagein_tcb_request(Return_frame *regs)
{
  //if ((*(Mword*)regs->pc & 0xfff00fff ) == 0xe5900000)
  if (*(Mword*)regs->pc == 0xe59ee000)
    {
      // printf("TCBR: %08lx\n", *(Mword*)regs->pc);
      // skip faulting instruction
      regs->pc += 4;
      // tell program that a pagefault occured we cannot handle
      regs->psr |= 0x40000000;	// set zero flag in psr
      regs->km_lr = 0;

      return true;
    }
  return false;
}

//---------------------------------------------------------------------------
IMPLEMENTATION [arm]:

#include "trap_state.h"


/** Constructor.
    @param space the address space
    @param id user-visible thread ID of the sender
    @param init_prio initial priority
    @param mcp thread's maximum controlled priority
    @post state() != Thread_invalid
 */
IMPLEMENT
Thread::Thread()
  : Sender(0),	// select optimized version of constructor
    _pager(Thread_ptr::Invalid),
    _exc_handler(Thread_ptr::Invalid),
    _del_observer(0)
{
  assert (state() == Thread_invalid);

  inc_ref();

  if (Config::stack_depth)
    std::memset((char*)this + sizeof(Thread), '5',
                Config::thread_block_size-sizeof(Thread)-64);

  // set a magic value -- we use it later to verify the stack hasn't
  // been overrun
  _magic = magic;
  _recover_jmpbuf = 0;
  _timeout = 0;
  _in_exception = false;

  *reinterpret_cast<void(**)()> (--_kernel_sp) = user_invoke;

  // clear out user regs that can be returned from the thread_ex_regs
  // system call to prevent covert channel
  Entry_frame *r = regs();
  r->sp(0);
  r->ip(0);
  r->psr = Proc::Status_mode_user;

  state_add(Thread_dead | Thread_suspended);

  // ok, we're ready to go!
}

IMPLEMENT inline
Mword
Thread::user_sp() const
{ return regs()->sp(); }

IMPLEMENT inline
void
Thread::user_sp(Mword sp)
{ return regs()->sp(sp); }

IMPLEMENT inline NEEDS[Thread::exception_triggered]
Mword
Thread::user_ip() const
{ return exception_triggered() ? _exc_cont.ip() : regs()->ip(); }

IMPLEMENT inline
Mword
Thread::user_flags() const
{ return 0; }

IMPLEMENT inline NEEDS[Thread::exception_triggered]
void
Thread::user_ip(Mword ip)
{
  if (exception_triggered())
    _exc_cont.ip(ip);
  else
    {
      Entry_frame *r = regs();
      r->ip(ip);
      r->psr = (r->psr & ~Proc::Status_mode_mask) | Proc::Status_mode_user;
    }
}


PUBLIC inline NEEDS ["trap_state.h"]
int
Thread::send_exception_arch(Trap_state *)
{
  // nothing to tweak on ARM
  return 1;
}

PRIVATE static inline
void
Thread::save_fpu_state_to_utcb(Trap_state *ts, Utcb *u)
{
  char *esu = (char *)&u->values[21];
  Fpu::save_user_exception_state(ts, (Fpu::Exception_state_user *)esu);
}

PRIVATE inline
bool
Thread::invalid_ipc_buffer(void const *a)
{
  if (!_in_exception)
    return Mem_layout::in_kernel(((Address)a & Config::SUPERPAGE_MASK)
                                 + Config::SUPERPAGE_SIZE - 1);

  return false;
}

PROTECTED inline
int
Thread::do_trigger_exception(Entry_frame *r, void *ret_handler)
{
  if (!_exc_cont.valid())
    {
      _exc_cont.activate(r, ret_handler);
      return 1;
    }
  return 0;
}


PRIVATE static inline
bool FIASCO_WARN_RESULT
Thread::copy_utcb_to_ts(L4_msg_tag const &tag, Thread *snd, Thread *rcv,
                        unsigned char rights)
{
  Trap_state *ts = (Trap_state*)rcv->_utcb_handler;
  Utcb *snd_utcb = snd->utcb().access();
  Mword       s  = tag.words();

  if (EXPECT_FALSE(rcv->exception_triggered()))
    {
      // triggered exception pending
      Mem::memcpy_mwords (ts, snd_utcb->values, s > 15 ? 15 : s);
      if (EXPECT_TRUE(s > 19))
	{
	  // sanitize processor mode
	  // XXX: fix race
	  snd_utcb->values[19] &= ~Proc::Status_mode_mask; // clear mode
	  snd_utcb->values[19] |=  Proc::Status_mode_supervisor
	    | Proc::Status_interrupts_disabled;

	  Continuation::User_return_frame const *s
	    = reinterpret_cast<Continuation::User_return_frame const *>((char*)&snd_utcb->values[15]);

	  rcv->_exc_cont.set(ts, s);
	}
    }
  else
    {
      Mem::memcpy_mwords (ts, snd_utcb->values, s > 18 ? 18 : s);
      if (EXPECT_TRUE(s > 18))
	ts->pc = snd_utcb->values[18];
      if (EXPECT_TRUE(s > 19))
	{
	  // sanitize processor mode
	  Mword p = snd_utcb->values[19];
	  p &= ~(Proc::Status_mode_mask | Proc::Status_interrupts_mask); // clear mode & irqs
	  p |=  Proc::Status_mode_user;
	  ts->psr = p;
	}
    }

  if (tag.transfer_fpu() && (rights & L4_fpage::W))
    snd->transfer_fpu(rcv);

  if ((tag.flags() & 0x8000) && (rights & L4_fpage::W))
    rcv->utcb().access()->user[2] = snd_utcb->values[25];

  bool ret = transfer_msg_items(tag, snd, snd_utcb,
                                rcv, rcv->utcb().access(), rights);

  rcv->state_del(Thread_in_exception);
  return ret;
}


PRIVATE static inline NEEDS[Thread::save_fpu_state_to_utcb]
bool FIASCO_WARN_RESULT
Thread::copy_ts_to_utcb(L4_msg_tag const &, Thread *snd, Thread *rcv,
                        unsigned char rights)
{
  Trap_state *ts = (Trap_state*)snd->_utcb_handler;

  {
    Lock_guard <Cpu_lock> guard (&cpu_lock);
    Utcb *rcv_utcb = rcv->utcb().access();

    Mem::memcpy_mwords (rcv_utcb->values, ts, 15);
    Continuation::User_return_frame *d
      = reinterpret_cast<Continuation::User_return_frame *>((char*)&rcv_utcb->values[15]);

    snd->_exc_cont.get(d, ts);


    if (EXPECT_TRUE(!snd->exception_triggered()))
      {
        rcv_utcb->values[18] = ts->pc;
        rcv_utcb->values[19] = ts->psr;
      }

    if (rcv_utcb->inherit_fpu() && (rights & L4_fpage::W))
      snd->transfer_fpu(rcv);

    save_fpu_state_to_utcb(ts, rcv_utcb);
  }
  return true;
}

PROTECTED inline
bool
Thread::invoke_arch(L4_msg_tag & /*tag*/, Utcb * /*utcb*/)
{
  return false;
}

PROTECTED inline
int
Thread::sys_control_arch(Utcb *)
{
  return 0;
}

PUBLIC static inline
bool
Thread::condition_valid(Unsigned32 insn, Unsigned32 psr)
{
  // Matrix of instruction conditions and PSR flags,
  // index into the table is the condition from insn
  Unsigned16 v[16] =
  {
    0xf0f0,
    0x0f0f,
    0xcccc,
    0x3333,
    0xff00,
    0x00ff,
    0xaaaa,
    0x5555,
    0x0c0c,
    0xf3f3,
    0xaa55,
    0x55aa,
    0x0a05,
    0xf5fa,
    0xffff,
    0xffff
  };

  return (v[insn >> 28] >> (psr >> 28)) & 1;
}

// ------------------------------------------------------------------------
IMPLEMENTATION [arm && armv6plus]:

PROTECTED inline
void
Thread::vcpu_resume_user_arch()
{
  // just an experiment for now, we cannot really take the
  // user-writable register because user-land might already use it
  asm volatile("mcr p15, 0, %0, c13, c0, 2"
               : : "r" (utcb().access(true)->values[25]) : "memory");
}

// ------------------------------------------------------------------------
IMPLEMENTATION [arm && !armv6plus]:

PROTECTED inline
void
Thread::vcpu_resume_user_arch()
{}

//-----------------------------------------------------------------------------
IMPLEMENTATION [mp]:

#include "ipi.h"

EXTENSION class Thread
{
private:
  static void kern_kdebug_ipi_entry() asm("kern_kdebug_ipi_entry");
};

PUBLIC static inline NEEDS["ipi.h"]
bool
Thread::check_for_ipi(unsigned irq)
{
  if (Ipi::is_ipi(irq))
    {
      switch (irq)
	{
	case Ipi::Request:
	  Thread::handle_remote_requests_irq();
	  break;
	case Ipi::Debug:
	  Ipi::eoi(Ipi::Debug);
	  kern_kdebug_ipi_entry();
	  break;
	case Ipi::Global_request:
	  handle_global_remote_requests_irq();
	  break;
	};
      return true;
    }

  return false;
}

//-----------------------------------------------------------------------------
IMPLEMENTATION [!mp]:

PUBLIC static inline
bool
Thread::check_for_ipi(unsigned)
{ return false; }

//-----------------------------------------------------------------------------
IMPLEMENTATION [arm && fpu]:

PUBLIC static
bool
Thread::handle_fpu_trap(Unsigned32 opcode, Trap_state *ts)
{
  if (!condition_valid(opcode, ts->psr))
    {
      if (ts->psr & Proc::Status_thumb)
        ts->pc += 2;
      return true;
    }

  if (Fpu::is_enabled())
    if ((opcode & 0x0ff00f90) == 0x0ef00a10)
      return Fpu::emulate_insns(opcode, ts, current_cpu());

#ifndef NDEBUG
  Thread *t = current_thread();
  if (!(t->state() & Thread_vcpu_enabled)
      && Fpu::is_enabled() && Fpu::owner(t->cpu()) == t)
    printf("KERNEL: FPU doesn't like us?\n");
  else
#endif
    if (current_thread()->switchin_fpu())
      {
        if ((opcode & 0x0ff00f90) == 0x0ef00a10)
          return Fpu::emulate_insns(opcode, ts, current_cpu());
        ts->pc -= (ts->psr & Proc::Status_thumb) ? 2 : 4;
        return true;
      }

  ts->error_code |= 0x01000000; // tag fpu undef insn
  if (Fpu::exc_pending())
    ts->error_code |= 0x02000000; // fpinst and fpinst2 in utcb will be valid

  return false;
}

PUBLIC static
void
Thread::init_fpu_trap_handling()
{
  handle_copro_fault[10] = Thread::handle_fpu_trap;
  handle_copro_fault[11] = Thread::handle_fpu_trap;
}

STATIC_INITIALIZEX(Thread, init_fpu_trap_handling);
