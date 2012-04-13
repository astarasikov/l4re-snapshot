INTERFACE [mp]:

#include "kernel_thread.h"

class App_cpu_thread : public Kernel_thread
{
private:
  void	bootstrap()		asm ("call_ap_bootstrap") FIASCO_FASTCALL;
};

IMPLEMENTATION [mp]:

#include <cstdlib>
#include <cstdio>

#include "config.h"
#include "delayloop.h"
#include "fpu.h"
#include "globals.h"
#include "helping_lock.h"
#include "kernel_task.h"
#include "processor.h"
#include "task.h"
#include "thread.h"
#include "thread_state.h"
#include "timer.h"
#include "spin_lock.h"


PUBLIC inline
Mword *
App_cpu_thread::init_stack()
{ return _kernel_sp; }

// the kernel bootstrap routine
IMPLEMENT
void
App_cpu_thread::bootstrap()
{
  extern Spin_lock<Mword> _tramp_mp_spinlock;

  state_change_dirty(0, Thread_ready);		// Set myself ready

  // Setup initial timeslice
  set_current_sched(sched());

  Fpu::init(cpu());

  // initialize the current_mem_space function to point to the kernel space
  Kernel_task::kernel_task()->mem_space()->make_current();

  Mem_unit::tlb_flush();

  Cpu::cpus.cpu(current_cpu()).set_online(1);

  _tramp_mp_spinlock.set(1);

  kernel_context(cpu(), this);
  Sched_context::rq(cpu()).set_idle(this->sched());

  Timer::enable();

  Per_cpu_data::run_late_ctors(cpu());

  cpu_lock.clear();

  printf("CPU[%u]: goes to idle loop\n", cpu());

  do_app_idle();
}

// ------------------------------------------------------------------------
IMPLEMENTATION [mp && !kernel_can_exit]:

PRIVATE inline
void
App_cpu_thread::do_app_idle()
{
  while (1)
    idle_op();
}

// ------------------------------------------------------------------------
IMPLEMENTATION [mp && kernel_can_exit]:

PRIVATE inline
void
App_cpu_thread::do_app_idle()
{
  while (running)
    {
      Mem::rmb();
      idle_op();
    }

  // Boost this thread's priority to the maximum. Since this thread tears down
  // all other threads, we want it to run whenever possible to advance as fast
  // as we can.
  ready_dequeue();
  sched()->set_prio(255);
  ready_enqueue();
  Proc::cli();
  while(1)
    Proc::halt();
}
