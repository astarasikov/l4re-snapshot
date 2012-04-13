INTERFACE:

#include "l4_buf_iter.h"
#include "l4_error.h"

class Syscall_frame;

EXTENSION class Thread
{
protected:
  struct Log_pf_invalid
  {
    Mword pfa;
    Mword cap_idx;
    Mword err;
  };

  struct Log_exc_invalid
  {
    Mword cap_idx;
  };

  enum Check_sender_result
  {
    Ok = 0,
    Queued = 2,
    Done = 4,
    Failed = 1,
  };

  Syscall_frame *_snd_regs;
  unsigned char _ipc_send_rights;
};

class Buf_utcb_saver
{
public:
  Buf_utcb_saver(Utcb const *u);
  void restore(Utcb *u);
private:
  L4_buf_desc buf_desc;
  Mword buf[2];
};

/**
 * Save critical contents of UTCB during nested IPC.
 */
class Pf_msg_utcb_saver : public Buf_utcb_saver
{
public:
  Pf_msg_utcb_saver(Utcb const *u);
  void restore(Utcb *u);
private:
  Mword msg[2];
};

// ------------------------------------------------------------------------
INTERFACE [debug]:

#include "tb_entry.h"

EXTENSION class Thread
{
protected:
  static unsigned log_fmt_pf_invalid(Tb_entry *, int max, char *buf) asm ("__fmt_page_fault_invalid_pager");
  static unsigned log_fmt_exc_invalid(Tb_entry *, int max, char *buf) asm ("__fmt_exception_invalid_handler");
};

// ------------------------------------------------------------------------
IMPLEMENTATION:

// IPC setup, and handling of ``short IPC'' and page-fault IPC

// IDEAS for enhancing this implementation: 

// Volkmar has suggested a possible optimization for
// short-flexpage-to-long-message-buffer transfers: Currently, we have
// to resort to long IPC in that case because the message buffer might
// contain a receive-flexpage option.  An easy optimization would be
// to cache the receive-flexpage option in the TCB for that case.
// This would save us the long-IPC setup because we wouldn't have to
// touch the receiver's user memory in that case.  Volkmar argues that
// cases like that are quite common -- for example, imagine a pager
// which at the same time is also a server for ``normal'' requests.

// The handling of cancel and timeout conditions could be improved as
// follows: Cancel and Timeout should not reset the ipc_in_progress
// flag.  Instead, they should just set and/or reset a flag of their
// own that is checked every time an (IPC) system call wants to go to
// sleep.  That would mean that IPCs that do not block are not
// cancelled or aborted.
//-

#include <cstdlib>		// panic()

#include "l4_types.h"
#include "l4_msg_item.h"

#include "config.h"
#include "cpu_lock.h"
#include "ipc_timeout.h"
#include "lock_guard.h"
#include "logdefs.h"
#include "map_util.h"
#include "processor.h"
#include "timer.h"
#include "kdb_ke.h"
#include "warn.h"

PUBLIC
virtual void
Thread::ipc_receiver_aborted()
{
  assert_kdb (receiver());

  sender_dequeue(receiver()->sender_list());
  receiver()->vcpu_update_state();
  set_receiver(0);

  // remote_ready_enqueue(): is only for mp
  activate();
}

PUBLIC inline
void
Thread::ipc_receiver_ready()
{
  vcpu_disable_irqs();
  state_change_dirty(~Thread_ipc_mask, Thread_receive_in_progress);
}

PRIVATE
void
Thread::ipc_send_msg(Receiver *recv)
{
  Syscall_frame *regs = _snd_regs;
  bool success = transfer_msg(regs->tag(), nonull_static_cast<Thread*>(recv), regs,
                              _ipc_send_rights);
  sender_dequeue(recv->sender_list());
  recv->vcpu_update_state();
  //printf("  done\n");
  regs->tag(L4_msg_tag(regs->tag(), success ? 0 : L4_msg_tag::Error));

  Mword state_del = Thread_ipc_mask | Thread_ipc_transfer;
  Mword state_add = Thread_ready;
  if (Receiver::prepared())
    // same as in Receiver::prepare_receive_dirty_2
    state_add |= Thread_receive_wait;

  if (cpu() == current_cpu())
    {
      state_change_dirty(~state_del, state_add);
      if (current_sched()->deblock(cpu(), current_sched(), true))
	recv->switch_to_locked(this);
    }
  else
    {
      drq_state_change(~state_del, state_add);
      current()->schedule_if(current()->handle_drq());
    }
}

PUBLIC virtual
void
Thread::modify_label(Mword const *todo, int cnt)
{
  assert_kdb (_snd_regs);
  Mword l = _snd_regs->from_spec();
  for (int i = 0; i < cnt*4; i += 4)
    {
      Mword const test_mask = todo[i];
      Mword const test      = todo[i+1];
      if ((l & test_mask) == test)
	{
	  Mword const del_mask = todo[i+2];
	  Mword const add_mask = todo[i+3];

	  l = (l & ~del_mask) | add_mask;
	  _snd_regs->from(l);
	  return;
	}
    }
}

PRIVATE inline
void
Thread::snd_regs(Syscall_frame *r)
{ _snd_regs = r; }


/** Page fault handler.
    This handler suspends any ongoing IPC, then sets up page-fault IPC.
    Finally, the ongoing IPC's state (if any) is restored.
    @param pfa page-fault virtual address
    @param error_code page-fault error code.
 */
PRIVATE
bool
Thread::handle_page_fault_pager(Thread_ptr const &_pager,
                                Address pfa, Mword error_code,
                                L4_msg_tag::Protocol protocol)
{
#ifndef NDEBUG
  // do not handle user space page faults from kernel mode if we're
  // already handling a request
  if (EXPECT_FALSE(!PF::is_usermode_error(error_code)
		   && thread_lock()->test() == Thread_lock::Locked))
    {
      kdb_ke("Fiasco BUG: page fault, under lock");
      panic("page fault in locked operation");
    }
#endif

  if (EXPECT_FALSE((state() & Thread_alien)))
    return false;

  Lock_guard<Cpu_lock> guard(&cpu_lock);

  unsigned char rights;
  Kobject_iface *pager = _pager.ptr(space(), &rights);

  if (!pager)
    {
      WARN("CPU%d: Pager of %lx is invalid (pfa=" L4_PTR_FMT
	   ", errorcode=" L4_PTR_FMT ") to %lx (pc=%lx)\n",
	   current_cpu(), dbg_id(), pfa, error_code,
           _pager.raw(), regs()->ip());


      LOG_TRACE("Page fault invalid pager", "pf", this,
                __fmt_page_fault_invalid_pager,
                Log_pf_invalid *l = tbe->payload<Log_pf_invalid>();
                l->cap_idx = _pager.raw();
                l->err     = error_code;
                l->pfa     = pfa);

      pager = this; // block on ourselves
    }

  // set up a register block used as an IPC parameter block for the
  // page fault IPC
  Syscall_frame r;
  Utcb *utcb = this->utcb().access(true);

  // save the UTCB fields affected by PF IPC
  Pf_msg_utcb_saver saved_utcb_fields(utcb);


  utcb->buf_desc = L4_buf_desc(0, 0, 0, L4_buf_desc::Inherit_fpu);
  utcb->buffers[0] = L4_msg_item::map(0).raw();
  utcb->buffers[1] = L4_fpage::all_spaces().raw();

  utcb->values[0] = PF::addr_to_msgword0 (pfa, error_code);
  utcb->values[1] = regs()->ip(); //PF::pc_to_msgword1 (regs()->ip(), error_code));

  L4_timeout_pair timeout(L4_timeout::Never, L4_timeout::Never);

  L4_msg_tag tag(2, 0, 0, protocol);

  r.timeout(timeout);
  r.tag(tag);
  r.from(0);
  r.ref(L4_obj_ref(_pager.raw() << L4_obj_ref::Cap_shift, L4_obj_ref::Ipc_call_ipc));
  pager->invoke(r.ref(), rights, &r, utcb);


  bool success = true;

  if (EXPECT_FALSE(r.tag().has_error()))
    {
      if (Config::conservative)
        {
          printf(" page fault %s error = 0x%lx\n",
                 utcb->error.snd_phase() ? "send" : "rcv",
                 utcb->error.raw());
	  kdb_ke("ipc to pager failed");
        }

      if (utcb->error.snd_phase()
          && (utcb->error.error() == L4_error::Not_existent)
          && PF::is_usermode_error(error_code)
	  && !(state() & Thread_cancel))
	{
	  success = false;
        }
    }
  else // no error
    {
      // If the pager rejects the mapping, it replies -1 in msg.w0
      if (EXPECT_FALSE (utcb->values[0] == Mword(-1)))
        success = false;
    }

  // restore previous IPC state

  saved_utcb_fields.restore(utcb);
  return success;
}

PRIVATE inline
Mword
Thread::check_sender(Thread *sender, bool timeout)
{
  if (EXPECT_FALSE(is_invalid()))
    {
      sender->utcb().access()->error = L4_error::Not_existent;
      return Failed;
    }

  if (EXPECT_FALSE(!sender_ok(sender)))
    {
      if (!timeout)
	{
	  sender->utcb().access()->error = L4_error::Timeout;
	  return Failed;
	}

      sender->set_receiver(this);
      sender->sender_enqueue(sender_list(), sender->sched_context()->prio());
      vcpu_set_irq_pending();
      return Queued;
    }

  return Ok;
}


PRIVATE inline NEEDS["timer.h"]
void Thread::goto_sleep(L4_timeout const &t, Sender *sender, Utcb *utcb)
{
  IPC_timeout timeout;

  if (EXPECT_FALSE(t.is_finite() && !_timeout))
    {
      state_del_dirty(Thread_ready);

      Unsigned64 sysclock = Timer::system_clock();
      Unsigned64 tval = t.microsecs(sysclock, utcb);

      if (EXPECT_TRUE((tval > sysclock)))
	{
	  set_timeout(&timeout);
	  timeout.set(tval, cpu());
	}
      else // timeout already hit
	state_change_dirty(~Thread_ipc_mask, Thread_ready | Thread_timeout);
    }
  else
    {
      if (EXPECT_TRUE(t.is_never()))
	state_del_dirty(Thread_ready);
      else
	state_change_dirty(~Thread_ipc_mask, Thread_ready | Thread_timeout);
    }

  if (sender == this)
    switch_sched(sched());

  schedule();

  if (EXPECT_FALSE((long)_timeout))
    {
      timeout.reset();
      set_timeout(0);
    }

  assert_kdb (state() & Thread_ready);
}



/**
 * @pre cpu_lock must be held
 */
PRIVATE inline NEEDS["logdefs.h"]
unsigned
Thread::handshake_receiver(Thread *partner, L4_timeout snd_t)
{
  assert_kdb(cpu_lock.test());

  switch (__builtin_expect(partner->check_sender(this, !snd_t.is_zero()), Ok))
    {
    case Failed:
      return Failed;
    case Queued:
      state_add_dirty(Thread_send_wait);
      return Queued;
    default:
      partner->state_change_dirty(~(Thread_ipc_mask | Thread_ready), Thread_ipc_transfer);
      return Ok;
    }
}

PRIVATE inline
void
Thread::set_ipc_error(L4_error const &e, Thread *rcv)
{
  utcb().access()->error = e;
  rcv->utcb().access()->error = L4_error(e, L4_error::Rcv);
}


PRIVATE inline
Sender *
Thread::get_next_sender(Sender *sender)
{
  if (sender_list()->head())
    {
      if (sender) // closed wait
	{
	  if (sender->in_sender_list() && this == sender->receiver())
	    return sender;
	}
      else // open wait
	{
	  Sender *next = Sender::cast(sender_list()->head());
	  assert_kdb (next->in_sender_list());
	  set_partner(next);
	  return next;
	}
    }
  return 0;
}


/**
 * Send an IPC message.
 *        Block until we can send the message or the timeout hits.
 * @param partner the receiver of our message
 * @param t a timeout specifier
 * @param regs sender's IPC registers
 * @pre cpu_lock must be held
 * @return sender's IPC error code
 */
PUBLIC
void
Thread::do_ipc(L4_msg_tag const &tag, bool have_send, Thread *partner,
               bool have_receive, Sender *sender,
               L4_timeout_pair t, Syscall_frame *regs,
               unsigned char rights)
{
  assert_kdb (cpu_lock.test());
  assert_kdb (this == current());

  bool do_switch = false;

  assert_kdb (!(state() & Thread_ipc_mask));

  prepare_receive(sender, have_receive ? regs : 0);
  bool activate_partner = false;

  if (have_send)
    {
      assert_kdb(!in_sender_list());
      do_switch = tag.do_switch();

      bool ok;
      unsigned result;

      set_ipc_send_rights(rights);

      if (EXPECT_FALSE(partner->cpu() != current_cpu()) ||
	  ((result = handshake_receiver(partner, t.snd)) == Failed
	   && partner->drq_pending()))
	{
	  // we have either per se X-CPU IPC or we ran into a
	  // IPC during migration (indicated by the pending DRQ)
	  do_switch = false;
	  result = remote_handshake_receiver(tag, partner, have_receive, t.snd,
	                                     regs, rights);
	}

      switch (__builtin_expect(result, Ok))
	{
	case Done:
	  ok = true;
	  break;

	case Queued:
	  // set _snd_regs, to enable active receiving
	  snd_regs(regs);
	  ok = do_send_wait(partner, t.snd); // --- blocking point ---
	  break;

	case Failed:
	  state_del_dirty(Thread_ipc_mask);
	  ok = false;
	  break;

	default:
	  // mmh, we can reset the receivers timeout
	  // ping pong with timeouts will profit from it, because
	  // it will require much less sorting overhead
	  // if we dont reset the timeout, the possibility is very high
	  // that the receiver timeout is in the timeout queue
	  partner->reset_timeout();

	  ok = transfer_msg(tag, partner, regs, rights);

	  // switch to receiving state
	  state_del_dirty(Thread_ipc_mask);
	  if (ok && have_receive)
	    state_add_dirty(Thread_receive_wait);

	  activate_partner = partner != this;
	  break;
	}

      if (EXPECT_FALSE(!ok))
        {
	  // send failed, so do not switch to receiver directly and skip receive phase
          have_receive = false;
          regs->tag(L4_msg_tag(0, 0, L4_msg_tag::Error, 0));
        }
    }
  else
    {
      assert_kdb (have_receive);
      state_add_dirty(Thread_receive_wait);
    }

  // only do direct switch on closed wait (call) or if we run on a foreign
  // scheduling context
  Sender *next = 0;

  have_receive = state() & Thread_receive_wait;

  if (have_receive)
    {
      assert_kdb (!in_sender_list());
      assert_kdb (!(state() & Thread_send_wait));
      next = get_next_sender(sender);
    }

  if (activate_partner)
    {
      if (partner->cpu() == current_cpu())
	{
	  Sched_context *cs = Sched_context::rq(cpu()).current_sched();
	  do_switch = do_switch && ((have_receive && sender) || cs->context() != this)
	              && !(next && current_sched()->dominates(cs));
	  partner->state_change_dirty(~Thread_ipc_transfer, Thread_ready);
	  if (do_switch)
	    schedule_if(handle_drq() || switch_exec_locked(partner, Context::Not_Helping));
	  else if (partner->current_sched()->deblock(current_cpu(), current_sched(), true))
	    switch_to_locked(partner);
	}
      else
	partner->drq_state_change(~Thread_ipc_transfer, Thread_ready);
    }

  if (next)
    {
      ipc_receiver_ready();
      next->ipc_send_msg(this);
      state_del_dirty(Thread_ipc_mask);
    }
  else if (have_receive)
    {
      if ((state() & Thread_full_ipc_mask) == Thread_receive_wait)
	goto_sleep(t.rcv, sender, utcb().access(true));
    }

  if (EXPECT_TRUE (!(state() & Thread_full_ipc_mask)))
    return;

  while (EXPECT_FALSE(state() & Thread_ipc_transfer))
    {
      state_del_dirty(Thread_ready);
      schedule();
    }

  if (EXPECT_TRUE (!(state() & Thread_full_ipc_mask)))
    return;

  Utcb *utcb = this->utcb().access(true);
  // the IPC has not been finished.  could be timeout or cancel
  // XXX should only modify the error-code part of the status code

  if (EXPECT_FALSE(state() & Thread_cancel))
    {
      // we've presumably been reset!
      regs->tag(commit_error(utcb, L4_error::R_canceled, regs->tag()));
    }
  else
    regs->tag(commit_error(utcb, L4_error::R_timeout, regs->tag()));
  state_del(Thread_full_ipc_mask);
}


PRIVATE inline NEEDS ["map_util.h", Thread::copy_utcb_to]
bool
Thread::transfer_msg(L4_msg_tag tag, Thread *receiver,
                     Syscall_frame *sender_regs, unsigned char rights)
{
  Syscall_frame* dst_regs = receiver->rcv_regs();

  bool success = copy_utcb_to(tag, receiver, rights);
  tag.set_error(!success);
  dst_regs->tag(tag);
  dst_regs->from(sender_regs->from_spec());

  // setup the reply capability in case of a call
  if (success && partner() == receiver)
    receiver->set_caller(this, rights);

  return success;
}



IMPLEMENT inline
Buf_utcb_saver::Buf_utcb_saver(const Utcb *u)
{
  buf_desc = u->buf_desc;
  buf[0] = u->buffers[0];
  buf[1] = u->buffers[1];
}

IMPLEMENT inline
void
Buf_utcb_saver::restore(Utcb *u)
{
  u->buf_desc = buf_desc;
  u->buffers[0] = buf[0];
  u->buffers[1] = buf[1];
}

IMPLEMENT inline
Pf_msg_utcb_saver::Pf_msg_utcb_saver(Utcb const *u) : Buf_utcb_saver(u)
{
  msg[0] = u->values[0];
  msg[1] = u->values[1];
}

IMPLEMENT inline
void
Pf_msg_utcb_saver::restore(Utcb *u)
{
  Buf_utcb_saver::restore(u);
  u->values[0] = msg[0];
  u->values[1] = msg[1];
}


/**
 * \pre must run with local IRQs disabled (CPU lock held)
 * to ensure that handler does not dissapear meanwhile.
 */
PRIVATE
bool
Thread::exception(Kobject_iface *handler, Trap_state *ts, Mword rights)
{
  Syscall_frame r;
  L4_timeout_pair timeout(L4_timeout::Never, L4_timeout::Never);

  CNT_EXC_IPC;

  void *old_utcb_handler = _utcb_handler;
  _utcb_handler = ts;

  // fill registers for IPC
  Utcb *utcb = this->utcb().access(true);
  Buf_utcb_saver saved_state(utcb);

  utcb->buf_desc = L4_buf_desc(0, 0, 0, L4_buf_desc::Inherit_fpu);
  utcb->buffers[0] = L4_msg_item::map(0).raw();
  utcb->buffers[1] = L4_fpage::all_spaces().raw();

  // clear regs
  L4_msg_tag tag(L4_exception_ipc::Msg_size, 0, L4_msg_tag::Transfer_fpu,
                 L4_msg_tag::Label_exception);

  r.tag(tag);
  r.timeout(timeout);
  r.from(0);
  r.ref(L4_obj_ref(_exc_handler.raw() << L4_obj_ref::Cap_shift, L4_obj_ref::Ipc_call_ipc));
  spill_user_state();
  handler->invoke(r.ref(), rights, &r, utcb);
  fill_user_state();

  saved_state.restore(utcb);

  if (EXPECT_FALSE(r.tag().has_error()))
    {
      if (Config::conservative)
        {
          printf(" exception fault %s error = 0x%lx\n",
                 utcb->error.snd_phase() ? "send" : "rcv",
                 utcb->error.raw());
	  kdb_ke("ipc to pager failed");
        }

      state_del(Thread_in_exception);
    }
   else if (r.tag().proto() == L4_msg_tag::Label_allow_syscall)
     state_add(Thread_dis_alien);

  // restore original utcb_handler
  _utcb_handler = old_utcb_handler;

  // FIXME: handle not existing pager properly
  // for now, just ignore any errors
  return 1;
}

/* return 1 if exception could be handled
 * return 0 if not for send_exception and halt thread
 */
PUBLIC inline NEEDS["task.h", "trap_state.h",
                    Thread::fast_return_to_user,
                    Thread::save_fpu_state_to_utcb]
int
Thread::send_exception(Trap_state *ts)
{
  assert(cpu_lock.test());

  Vcpu_state *vcpu = vcpu_state().access();

  if (vcpu_exceptions_enabled(vcpu))
    {
      // do not reflect debug exceptions to the VCPU but handle them in
      // Fiasco
      if (EXPECT_FALSE(ts->is_debug_exception()
                       && !(vcpu->state & Vcpu_state::F_debug_exc)))
        return 0;

      if (_exc_cont.valid())
	return 1;
      if (vcpu_enter_kernel_mode(vcpu))
	{
	  // enter_kernel_mode has switched the address space from user to
	  // kernel space, so reevaluate the address of the VCPU state area
	  vcpu = vcpu_state().access();
	}

      spill_user_state();
      LOG_TRACE("VCPU events", "vcpu", this, __context_vcpu_log_fmt,
	  Vcpu_log *l = tbe->payload<Vcpu_log>();
	  l->type = 2;
	  l->state = vcpu->_saved_state;
	  l->ip = ts->ip();
	  l->sp = ts->sp();
	  l->trap = ts->trapno();
	  l->err = ts->error();
	  l->space = vcpu_user_space() ? static_cast<Task*>(vcpu_user_space())->dbg_id() : ~0;
	  );
      memcpy(&vcpu->_ts, ts, sizeof(Trap_state));
      save_fpu_state_to_utcb(ts, utcb().access());
      fast_return_to_user(vcpu->_entry_ip, vcpu->_sp, vcpu_state().usr().get());
    }

  // local IRQs must be disabled because we dereference a Thread_ptr
  if (EXPECT_FALSE(_exc_handler.is_kernel()))
    return 0;

  if (!send_exception_arch(ts))
    return 0; // do not send exception

  unsigned char rights = 0;
  Kobject_iface *pager = _exc_handler.ptr(space(), &rights);

  if (EXPECT_FALSE(!pager))
    {
      /* no pager (anymore), just ignore the exception, return success */
      LOG_TRACE("Exception invalid handler", "exc", this,
                __fmt_exception_invalid_handler,
                Log_exc_invalid *l = tbe->payload<Log_exc_invalid>();
                l->cap_idx = _exc_handler.raw());
      if (EXPECT_FALSE(space() == sigma0_task))
	{
	  ts->dump();
	  WARNX(Error, "Sigma0 raised an exception --> HALT\n");
	  panic("...");
	}

      pager = this; // block on ourselves
    }

  state_change(~Thread_cancel, Thread_in_exception);

  return exception(pager, ts, rights);
}

PRIVATE static
bool
Thread::try_transfer_local_id(L4_buf_iter::Item const *const buf,
                              L4_fpage sfp, Mword *rcv_word, Thread* snd,
                              Thread *rcv)
{
  if (buf->b.is_rcv_id())
    {
      if (snd->space() == rcv->space())
	{
	  rcv_word[-2] |= 6;
	  rcv_word[-1] = sfp.raw();
	  return true;
	}
      else
	{
	  unsigned char rights = 0;
	  Obj_space::Capability cap = snd->space()->obj_space()->lookup(sfp.obj_index());
	  Kobject_iface *o = cap.obj();
	  rights = cap.rights();
	  if (EXPECT_TRUE(o && o->is_local(rcv->space())))
	    {
	      rcv_word[-2] |= 4;
	      rcv_word[-1] = o->obj_id() | Mword(rights);
	      return true;
	    }
	}
    }
  return false;
}

PRIVATE static inline
bool FIASCO_WARN_RESULT
Thread::copy_utcb_to_utcb(L4_msg_tag const &tag, Thread *snd, Thread *rcv,
                          unsigned char rights)
{
  assert (cpu_lock.test());

  Utcb *snd_utcb = snd->utcb().access();
  Utcb *rcv_utcb = rcv->utcb().access();
  Mword s = tag.words();
  Mword r = Utcb::Max_words;

  Mem::memcpy_mwords(rcv_utcb->values, snd_utcb->values, r < s ? r : s);

  bool success = true;
  if (tag.items())
    success = transfer_msg_items(tag, snd, snd_utcb, rcv, rcv_utcb, rights);

  if (tag.transfer_fpu() && rcv_utcb->inherit_fpu() && (rights & L4_fpage::W))
    snd->transfer_fpu(rcv);

  return success;
}


PUBLIC inline NEEDS[Thread::copy_utcb_to_ts, Thread::copy_utcb_to_utcb,
                    Thread::copy_ts_to_utcb]
bool FIASCO_WARN_RESULT
Thread::copy_utcb_to(L4_msg_tag const &tag, Thread* receiver,
                     unsigned char rights)
{
  // we cannot copy trap state to trap state!
  assert_kdb (!this->_utcb_handler || !receiver->_utcb_handler);
  if (EXPECT_FALSE(this->_utcb_handler != 0))
    return copy_ts_to_utcb(tag, this, receiver, rights);
  else if (EXPECT_FALSE(receiver->_utcb_handler != 0))
    return copy_utcb_to_ts(tag, this, receiver, rights);
  else
    return copy_utcb_to_utcb(tag, this, receiver, rights);
}

PRIVATE static
bool
Thread::transfer_msg_items(L4_msg_tag const &tag, Thread* snd, Utcb *snd_utcb,
                           Thread *rcv, Utcb *rcv_utcb,
                           unsigned char rights)
{
  // LOG_MSG_3VAL(current(), "map bd=", rcv_utcb->buf_desc.raw(), 0, 0);
  L4_buf_iter mem_buffer(rcv_utcb, rcv_utcb->buf_desc.mem());
  L4_buf_iter io_buffer(rcv_utcb, rcv_utcb->buf_desc.io());
  L4_buf_iter obj_buffer(rcv_utcb, rcv_utcb->buf_desc.obj());
  L4_snd_item_iter snd_item(snd_utcb, tag.words());
  register int items = tag.items();
  Mword *rcv_word = rcv_utcb->values + tag.words();

  // XXX: damn X-CPU state modification
  // snd->prepare_long_ipc(rcv);
  Reap_list rl;

  for (;items > 0 && snd_item.more();)
    {
      if (EXPECT_FALSE(!snd_item.next()))
	{
	  snd->set_ipc_error(L4_error::Overflow, rcv);
	  return false;
	}

      L4_snd_item_iter::Item const *const item = snd_item.get();

      if (item->b.is_void())
	{ // XXX: not sure if void fpages are needed
	  // skip send item and current rcv_buffer
	  --items;
	  continue;
	}

      L4_buf_iter *buf_iter = 0;

      switch (item->b.type())
	{
	case L4_msg_item::Map:
	  switch (L4_fpage(item->d).type())
	    {
	    case L4_fpage::Memory: buf_iter = &mem_buffer; break;
	    case L4_fpage::Io:     buf_iter = &io_buffer; break;
	    case L4_fpage::Obj:    buf_iter = &obj_buffer; break;
	    default: break;
	    }
	  break;
	default:
	  break;
	}

      if (EXPECT_FALSE(!buf_iter))
	{
	  // LOG_MSG_3VAL(snd, "lIPCm0", 0, 0, 0);
	  snd->set_ipc_error(L4_error::Overflow, rcv);
	  return false;
	}

      L4_buf_iter::Item const *const buf = buf_iter->get();

      if (EXPECT_FALSE(buf->b.is_void() || buf->b.type() != item->b.type()))
	{
	  // LOG_MSG_3VAL(snd, "lIPCm1", buf->b.raw(), item->b.raw(), 0);
	  snd->set_ipc_error(L4_error::Overflow, rcv);
	  return false;
	}

	{
	  assert_kdb (item->b.type() == L4_msg_item::Map);
	  L4_fpage sfp(item->d);
	  *rcv_word = (item->b.raw() & ~0x0ff7) | (sfp.raw() & 0x0ff0);

	  rcv_word += 2;

	  if (!try_transfer_local_id(buf, sfp, rcv_word, snd, rcv))
	    {
	      // we need to do a real mapping�

	      // diminish when sending via restricted ipc gates
	      if (sfp.type() == L4_fpage::Obj)
		sfp.mask_rights(L4_fpage::Rights(rights | L4_fpage::RX));
	      cpu_lock.clear();
	      L4_error err = fpage_map(snd->space(), sfp,
		  rcv->space(), L4_fpage(buf->d), item->b, &rl);
	      cpu_lock.lock();

	      if (EXPECT_FALSE(!err.ok()))
		{
		  snd->set_ipc_error(err, rcv);
		  return false;
		}
	    }
	}

      --items;

      if (!item->b.compound())
	buf_iter->next();
    }

  if (EXPECT_FALSE(items))
    {
      snd->set_ipc_error(L4_error::Overflow, rcv);
      return false;
    }

  return true;
}


/**
 * \pre Runs on the sender CPU
 */
PRIVATE inline
bool
Thread::abort_send(L4_error const &e, Thread *partner)
{
  state_del_dirty(Thread_full_ipc_mask);

  if (_timeout && _timeout->is_set())
    _timeout->reset();

  set_timeout(0);
  Abort_state abt = Abt_ipc_done;

  if (partner->cpu() == current_cpu())
    {
      if (in_sender_list())
	{
	  sender_dequeue(partner->sender_list());
	  partner->vcpu_update_state();
	  abt = Abt_ipc_cancel;

	}
      else if (partner->in_ipc(this))
	abt = Abt_ipc_in_progress;
    }
  else
    abt = partner->Receiver::abort_send(this);

  switch (abt)
    {
    default:
    case Abt_ipc_done:
      return true;
    case Abt_ipc_cancel:
      utcb().access()->error = e;
      return false;
    case Abt_ipc_in_progress:
      state_add_dirty(Thread_ipc_transfer);
      while (state() & Thread_ipc_transfer)
	{
	  state_del_dirty(Thread_ready);
	  schedule();
	}
      return true;
    }
}



/**
 * \pre Runs on the sender CPU
 */
PRIVATE inline
bool
Thread::do_send_wait(Thread *partner, L4_timeout snd_t)
{
  IPC_timeout timeout;

  if (EXPECT_FALSE(snd_t.is_finite()))
    {
      Unsigned64 tval = snd_t.microsecs(Timer::system_clock(), utcb().access(true));
      // Zero timeout or timeout expired already -- give up
      if (tval == 0)
	return abort_send(L4_error::Timeout, partner);

      set_timeout(&timeout);
      timeout.set(tval, cpu());
    }

  register Mword ipc_state;

  while (((ipc_state = state() & (Thread_send_wait | Thread_ipc_abort_mask))) == Thread_send_wait)
    {
      state_del_dirty(Thread_ready);
      schedule();
    }

  if (EXPECT_FALSE(ipc_state == (Thread_cancel | Thread_send_wait)))
    return abort_send(L4_error::Canceled, partner);

  if (EXPECT_FALSE(ipc_state == (Thread_timeout | Thread_send_wait)))
    return abort_send(L4_error::Timeout, partner);

  timeout.reset();
  set_timeout(0);

  return true;
}

PRIVATE inline
void
Thread::set_ipc_send_rights(unsigned char c)
{
  _ipc_send_rights = c;
}

//---------------------------------------------------------------------
IMPLEMENTATION [!mp]:

PRIVATE inline NEEDS ["l4_types.h"]
unsigned
Thread::remote_handshake_receiver(L4_msg_tag const &, Thread *,
                                  bool, L4_timeout, Syscall_frame *, unsigned char)
{
  kdb_ke("Remote IPC in UP kernel");
  return Failed;
}

//---------------------------------------------------------------------
INTERFACE [mp]:

struct Ipc_remote_request;

struct Ipc_remote_request
{
  L4_msg_tag tag;
  Thread *partner;
  Syscall_frame *regs;
  unsigned char rights;
  bool timeout;
  bool have_rcv;

  unsigned result;
};

struct Ready_queue_request
{
  Thread *thread;
  Mword state_add;
  Mword state_del;

  enum Result { Done, Wrong_cpu, Not_existent };
  Result result;
};

//---------------------------------------------------------------------
IMPLEMENTATION [mp]:


PRIVATE inline NOEXPORT
bool
Thread::remote_ipc_send(Context *src, Ipc_remote_request *rq)
{
  (void)src;
  // LOG_MSG_3VAL(this, "rse", current_cpu(), (Mword)src, (Mword)this);
#if 0
  LOG_MSG_3VAL(this, "rsend", (Mword)src, 0, 0);
  printf("CPU[%u]: remote IPC send ...\n"
         "  partner=%p [%u]\n"
	 "  sender =%p [%u] regs=%p\n"
	 "  timeout=%u\n",
	 current_cpu(),
	 rq->partner, rq->partner->cpu(),
	 src, src->cpu(),
	 rq->regs,
	 rq->timeout);
#endif

  switch (__builtin_expect(rq->partner->check_sender(this, rq->timeout), Ok))
    {
    case Failed:
      rq->result = Failed;
      return false;
    case Queued:
      rq->result = Queued;
      return false;
    default:
      break;
    }

  // trigger remote_ipc_receiver_ready path, because we may need to grab locks
  // and this is forbidden in a DRQ handler. So transfer the IPC in usual
  // thread code. However, this induces a overhead of two extra IPIs.
  if (rq->tag.items())
    {
      //LOG_MSG_3VAL(rq->partner, "pull", dbg_id(), 0, 0);
      rq->partner->state_change_dirty(~(Thread_ipc_mask | Thread_ready), Thread_ipc_transfer);
      rq->result = Ok;
      return true;
    }
  rq->partner->vcpu_disable_irqs();
  bool success = transfer_msg(rq->tag, rq->partner, rq->regs, _ipc_send_rights);
  rq->result = success ? Done : Failed;

  rq->partner->state_change_dirty(~Thread_ipc_mask, Thread_ready);
  // hm, should be done by lazy queueing: rq->partner->ready_enqueue();
  return true;
}

PRIVATE static
unsigned
Thread::handle_remote_ipc_send(Drq *src, Context *, void *_rq)
{
  Ipc_remote_request *rq = (Ipc_remote_request*)_rq;
  bool r = nonull_static_cast<Thread*>(src->context())->remote_ipc_send(src->context(), rq);
  //LOG_MSG_3VAL(src, "rse<", current_cpu(), (Mword)src, r);
  return r ? Drq::Need_resched : 0;
}

/**
 * \pre Runs on the sender CPU
 */
PRIVATE //inline NEEDS ["mp_request.h"]
unsigned
Thread::remote_handshake_receiver(L4_msg_tag const &tag, Thread *partner,
                                  bool have_receive,
                                  L4_timeout snd_t, Syscall_frame *regs,
                                  unsigned char rights)
{
  // Flag that there must be no switch in the receive path.
  // This flag also prevents the receive path from accessing
  // the thread state of a remote sender.
  Ipc_remote_request rq;
  rq.tag = tag;
  rq.have_rcv = have_receive;
  rq.partner = partner;
  rq.timeout = !snd_t.is_zero();
  rq.regs = regs;
  rq.rights = rights;
  _snd_regs = regs;

  set_receiver(partner);

  state_add_dirty(Thread_send_wait);

  partner->drq(handle_remote_ipc_send, &rq,
               remote_prepare_receive);

  return rq.result;
}

PRIVATE static
unsigned
Thread::remote_prepare_receive(Drq *src, Context *, void *arg)
{
  Context *c = src->context();
  Ipc_remote_request *rq = (Ipc_remote_request*)arg;
  //printf("CPU[%2u:%p]: remote_prepare_receive (err=%x)\n", current_cpu(), c, rq->err.error());

  // No atomic switch to receive state if we are queued, or the IPC must be done by
  // the sender's CPU
  if (EXPECT_FALSE(rq->result == Queued || rq->result == Ok))
    return 0;

  c->state_del(Thread_ipc_mask);
  if (EXPECT_FALSE((rq->result & Failed) || !rq->have_rcv))
    return 0;

  c->state_add_dirty(Thread_receive_wait);
  return 0;
}

//---------------------------------------------------------------------------
IMPLEMENTATION [debug]:

IMPLEMENT
unsigned
Thread::log_fmt_pf_invalid(Tb_entry *e, int max, char *buf)
{
  Log_pf_invalid *l = e->payload<Log_pf_invalid>();
  return snprintf(buf, max, "InvCap C:%lx pfa=%lx err=%lx", l->cap_idx, l->pfa, l->err);
}

IMPLEMENT
unsigned
Thread::log_fmt_exc_invalid(Tb_entry *e, int max, char *buf)
{
  Log_exc_invalid *l = e->payload<Log_exc_invalid>();
  return snprintf(buf, max, "InvCap C:%lx", l->cap_idx);
}
