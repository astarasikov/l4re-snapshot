IMPLEMENTATION [ia32,amd64,ux]:

#include <cassert>
#include <cstdio>

#include "cpu.h"
#include "globals.h"		// current()
#include "kmem.h"
#include "lock_guard.h"
#include "space.h"
#include "thread_state.h"


IMPLEMENT inline
void
Context::spill_user_state()
{}

IMPLEMENT inline
void
Context::fill_user_state()
{}


/** Thread context switchin.  Called on every re-activation of a thread
    (switch_exec()).  This method is public only because it is called from
    from assembly code in switch_cpu().
 */
IMPLEMENT
void
Context::switchin_context(Context *from)
{
  assert_kdb (this == current());
  assert_kdb (state() & Thread_ready_mask);

  // Set kernel-esp in case we want to return to the user.
  // kmem::kernel_sp() returns a pointer to the kernel SP (in the
  // TSS) the CPU uses when next switching from user to kernel mode.
  // regs() + 1 returns a pointer to the end of our kernel stack.
  Cpu::cpus.cpu(cpu()).kernel_sp() = reinterpret_cast<Address>(regs() + 1);

  // switch to our page directory if necessary
  vcpu_aware_space()->switchin_context(from->vcpu_aware_space());

  // load new segment selectors
  load_segments();

  // update the global UTCB pointer to make the thread find its UTCB
  // using fs:[0]
  Mem_layout::user_utcb_ptr(current_cpu()) = utcb().usr();
}

//---------------------------------------------------------------------------
IMPLEMENTATION [ia32 || ux]:

PROTECTED inline NEEDS["cpu.h"]
void
Context::load_segments()
{
  Cpu::set_es((Unsigned32)_es);
  Cpu::set_fs((Unsigned32)_fs);
  Cpu::set_gs((Unsigned32)_gs);
}

PROTECTED inline NEEDS["cpu.h"]
void
Context::store_segments()
{
  _es = Cpu::get_es();
  _fs = Cpu::get_fs();
  _gs = Cpu::get_gs();
}


//---------------------------------------------------------------------------
IMPLEMENTATION [amd64]:

PROTECTED inline
void
Context::load_segments()
{}

PROTECTED inline
void
Context::store_segments()
{}

PROTECTED inline
void
Context::switch_gdt_user_entries(Context *)
{}
