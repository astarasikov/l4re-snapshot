IMPLEMENTATION:

#include "thread_object.h"
#include "thread_state.h"

extern "C"
void
sys_ipc_wrapper()
{
  assert_kdb (!(current()->state() & Thread_drq_ready));

  Thread *curr = current_thread();
  Syscall_frame *f = curr->regs();

  Obj_cap obj = f->ref();
  Utcb *utcb = curr->utcb().access(true);
  unsigned char rights;
  Kobject_iface *o = obj.deref(&rights);
  L4_msg_tag e;
  if (EXPECT_TRUE(o!=0))
    o->invoke(obj, rights, f, utcb);
  else
    f->tag(curr->commit_error(utcb, L4_error::Not_existent));
}


//---------------------------------------------------------------------------
IMPLEMENTATION [debug]:

#include "space.h"
#include "task.h"

extern "C" void sys_invoke_debug(Kobject_iface *o, Syscall_frame *f) __attribute__((weak));

extern "C" void sys_invoke_debug_wrapper()
{
  Thread *curr = current_thread();
  Syscall_frame *f = curr->regs();
  //printf("sys_invoke_debugger(f=%p, obj=%lx)\n", f, f->ref().raw());
  Kobject_iface *o = curr->space()->obj_space()->lookup_local(f->ref().cap());
  if (o && &::sys_invoke_debug)
    ::sys_invoke_debug(o, f);
  else
    f->tag(curr->commit_error(curr->utcb().access(true), L4_error::Not_existent));
}

//---------------------------------------------------------------------------
IMPLEMENTATION [!debug]:

#include "thread.h"

extern "C" void sys_invoke_debug_wrapper() {}

//---------------------------------------------------------------------------
INTERFACE [ia32 || ux || amd64]:

extern void (*syscall_table[])();


//---------------------------------------------------------------------------
IMPLEMENTATION [ia32 || ux || amd64]:

void (*syscall_table[])() =
{
  sys_ipc_wrapper,
  0,
  sys_invoke_debug_wrapper,
};

