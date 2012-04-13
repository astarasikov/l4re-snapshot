INTERFACE:

#include "kobject.h"
#include "kobject_helper.h"
#include "ref_ptr.h"
#include "slab_cache_anon.h"
#include "thread_object.h"

class Ram_quota;

class Ipc_gate_obj;

class Ipc_gate_ctl : public Kobject_h<Ipc_gate_ctl, Kobject_iface>
{
private:
  enum Operation
  {
    Op_bind     = 0x10,
    Op_get_info = 0x11,
  };
};

class Ipc_gate : public Kobject
{
  friend class Ipc_gate_ctl;
protected:

  Ref_ptr<Thread> _thread;
  Mword _id;
  Ram_quota *_quota;
  Locked_prio_list _wait_q;
};

class Ipc_gate_obj : public Ipc_gate, public Ipc_gate_ctl
{
  FIASCO_DECLARE_KOBJ();

private:
  friend class Ipc_gate;
  typedef slab_cache_anon Self_alloc;

public:
  bool put() { return Ipc_gate::put(); }

  Thread *thread() const { return _thread.ptr(); }
  Mword id() const { return _id; }
  Mword obj_id() const { return _id; }
  bool is_local(Space *s) const { return _thread->space() == s; }
};

//---------------------------------------------------------------------------
INTERFACE [debug]:

EXTENSION class Ipc_gate
{
protected:
  struct Log_ipc_gate_invoke
  {
    Mword gate_dbg_id;
    Mword thread_dbg_id;
    Mword label;
  };

  static unsigned log_fmt(Tb_entry *, int max, char *buf) asm ("__fmt_ipc_gate_invoke");
};

//---------------------------------------------------------------------------
IMPLEMENTATION:

#include <cstddef>

#include "entry_frame.h"
#include "ipc_timeout.h"
#include "kmem_slab.h"
#include "logdefs.h"
#include "ram_quota.h"
#include "static_init.h"
#include "thread.h"
#include "thread_state.h"
#include "timer.h"

FIASCO_DEFINE_KOBJ(Ipc_gate_obj);

PUBLIC
::Kobject_mappable *
Ipc_gate_obj::map_root()
{ return Ipc_gate::map_root(); }

PUBLIC
Kobject_iface *
Ipc_gate_obj::downgrade(unsigned long attr)
{
  if (attr & L4_msg_item::C_obj_right_1)
    return static_cast<Ipc_gate*>(this);
  else
    return static_cast<Ipc_gate_ctl*>(this);
}

PUBLIC inline
Ipc_gate::Ipc_gate(Ram_quota *q, Thread *t, Mword id)
  : _thread(t), _id(id), _quota(q), _wait_q()
{}

PUBLIC inline
Ipc_gate_obj::Ipc_gate_obj(Ram_quota *q, Thread *t, Mword id)
  : Ipc_gate(q, t, id)
{}

PUBLIC
void
Ipc_gate_obj::unblock_all()
{
  while (::Prio_list_elem *h = _wait_q.head())
    {
      Lock_guard<Cpu_lock> g1(&cpu_lock);
      Thread *w;
	{
	  Lock_guard<typeof(_wait_q)> g2(&_wait_q);
	  if (EXPECT_FALSE(h != _wait_q.head()))
	    continue;

	  w = static_cast<Thread*>(Sender::cast(h));
	  w->sender_dequeue(&_wait_q);
	}
      w->activate();
    }
}

PUBLIC virtual
void
Ipc_gate_obj::initiate_deletion(Kobject ***r)
{
  if (_thread)
    _thread->ipc_gate_deleted(_id);

  Kobject::initiate_deletion(r);
}

PUBLIC virtual
void
Ipc_gate_obj::destroy(Kobject ***r)
{
  Kobject::destroy(r);
  _thread = 0;
  unblock_all();
}

PUBLIC
Ipc_gate_obj::~Ipc_gate_obj()
{
  unblock_all();
}

PUBLIC inline NEEDS[<cstddef>]
void *
Ipc_gate_obj::operator new (size_t, void *b)
{ return b; }

static Kmem_slab_t<Ipc_gate_obj> _ipc_gate_allocator("Ipc_gate");

PRIVATE static
Ipc_gate_obj::Self_alloc *
Ipc_gate_obj::allocator()
{ return &_ipc_gate_allocator; }

PUBLIC static
Ipc_gate_obj *
Ipc_gate::create(Ram_quota *q, Thread *t, Mword id)
{
  void *nq;
  if (q->alloc(sizeof(Ipc_gate_obj)))
    {
      if (nq = Ipc_gate_obj::allocator()->alloc())
	return new (nq) Ipc_gate_obj(q, t, id);
      else
	q->free(sizeof(Ipc_gate_obj));
    }

  return 0;
}

PUBLIC
void Ipc_gate_obj::operator delete (void *_f)
{
  register Ipc_gate_obj *f = (Ipc_gate_obj*)_f;
  Ram_quota *p = f->_quota;

  if (p)
    p->free(sizeof(Ipc_gate_obj));

  allocator()->free(f);
}

PRIVATE inline NOEXPORT
L4_msg_tag
Ipc_gate_ctl::bind_thread(L4_obj_ref, Mword, Syscall_frame *f, Utcb const *in, Utcb *)
{
  L4_msg_tag tag = f->tag();
  L4_snd_item_iter snd_items(in, tag.words());

  if (tag.words() < 2 || !tag.items() || !snd_items.next())
    return commit_result(-L4_err::EInval);

  L4_fpage bind_thread(snd_items.get()->d);
  if (EXPECT_FALSE(!bind_thread.is_objpage()))
    return commit_error(in, L4_error::Overflow);

  register Context *const c_thread = ::current();
  register Space *const c_space = c_thread->space();
  register Obj_space *const o_space = c_space->obj_space();
  unsigned char t_rights = 0;
  Thread *t = Kobject::dcast<Thread_object*>(o_space->lookup_local(bind_thread.obj_index(), &t_rights));

  if (!(t_rights & L4_fpage::CS))
    return commit_result(-L4_err::EPerm);


  Ipc_gate_obj *g = static_cast<Ipc_gate_obj*>(this);
  g->_id = in->values[1];
  Mem::mp_wmb();
  g->_thread = t;
  Mem::mp_wmb();
  g->unblock_all();
  c_thread->rcu_wait();
  g->unblock_all();

  return commit_result(0);
}

PRIVATE inline NOEXPORT
L4_msg_tag
Ipc_gate_ctl::get_infos(L4_obj_ref, Mword, Syscall_frame *, Utcb const *, Utcb *out)
{
  Ipc_gate_obj *g = static_cast<Ipc_gate_obj*>(this);
  out->values[0] = g->_id;
  return commit_result(0, 1);
}

PUBLIC
void
Ipc_gate_ctl::invoke(L4_obj_ref self, Mword rights, Syscall_frame *f, Utcb *utcb)
{
  if (f->tag().proto() == L4_msg_tag::Label_kobject)
    Kobject_h<Ipc_gate_ctl, Kobject_iface>::invoke(self, rights, f, utcb);
  else
    static_cast<Ipc_gate_obj*>(this)->Ipc_gate::invoke(self, rights, f, utcb);
}


PUBLIC
L4_msg_tag
Ipc_gate_ctl::kinvoke(L4_obj_ref self, Mword rights, Syscall_frame *f, Utcb const *in, Utcb *out)
{
  L4_msg_tag tag = f->tag();

  if (EXPECT_FALSE(tag.proto() != L4_msg_tag::Label_kobject))
    return commit_result(-L4_err::EBadproto);

  if (EXPECT_FALSE(tag.words() < 1))
    return commit_result(-L4_err::EInval);

  switch (in->values[0])
    {
    case Op_bind:
      return bind_thread(self, rights, f, in, out);
    case Op_get_info:
      return get_infos(self, rights, f, in, out);
    default:
      return static_cast<Ipc_gate_obj*>(this)->kobject_invoke(self, rights, f, in, out);
    }
}

PRIVATE inline NOEXPORT
L4_error
Ipc_gate::block(Thread *ct, L4_timeout const &to, Utcb *u)
{
  Unsigned64 t = 0;
  if (!to.is_never())
    {
      t = to.microsecs(Timer::system_clock(), u);
      if (!t)
	return L4_error::Timeout;
    }

    {
      Lock_guard<typeof(_wait_q)> g(&_wait_q);
      ct->wait_queue(&_wait_q);
      ct->sender_enqueue(&_wait_q, ct->sched_context()->prio());
    }
  ct->state_change_dirty(~Thread_ready, Thread_send_wait);

  IPC_timeout timeout;
  if (t)
    {
      timeout.set(t, ct->cpu());
      ct->set_timeout(&timeout);
    }

  ct->schedule();

  ct->state_change(~Thread_ipc_mask, Thread_ready);
  ct->reset_timeout();

  if (EXPECT_FALSE(ct->in_sender_list() && timeout.has_hit()))
    {
      Lock_guard<typeof(_wait_q)> g(&_wait_q);
      if (!ct->in_sender_list())
	return L4_error::None;

      ct->sender_dequeue(&_wait_q);
      return L4_error::Timeout;
    }
  return L4_error::None;
}


PUBLIC
void
Ipc_gate::invoke(L4_obj_ref /*self*/, Mword rights, Syscall_frame *f, Utcb *utcb)
{
  Syscall_frame *ipc_f = f;
  //LOG_MSG_3VAL(current(), "gIPC", Mword(_thread), _id, f->obj_2_flags());
  //printf("Invoke: Ipc_gate(%lx->%p)...\n", _id, _thread);
  Thread *ct = current_thread();
  Thread *sender = 0;
  Thread *partner = 0;
  bool have_rcv = false;

  if (EXPECT_FALSE(!_thread.ptr()))
    {
      L4_error e = block(ct, f->timeout().snd, utcb);
      if (!e.ok())
	{
	  f->tag(commit_error(utcb, e));
	  return;
	}

      if (EXPECT_FALSE(!_thread.ptr()))
	{
	  f->tag(commit_error(utcb, L4_error::Not_existent));
	  return;
	}
    }

  bool ipc = _thread->check_sys_ipc(f->ref().op(), &partner, &sender, &have_rcv);

  LOG_TRACE("IPC Gate invoke", "gate", current(), __fmt_ipc_gate_invoke,
      Log_ipc_gate_invoke *l = tbe->payload<Log_ipc_gate_invoke>();
      l->gate_dbg_id = dbg_id();
      l->thread_dbg_id = _thread->dbg_id();
      l->label = _id | rights;
  );

  if (EXPECT_FALSE(!ipc))
    f->tag(commit_error(utcb, L4_error::Not_existent));
  else
    {
      ipc_f->from(_id | rights);
      ct->do_ipc(f->tag(), partner, partner, have_rcv, sender,
                 f->timeout(), f, rights);
    }
}

//---------------------------------------------------------------------------
IMPLEMENTATION [debug]:

PUBLIC
::Kobject_dbg *
Ipc_gate_obj::dbg_info() const
{ return Ipc_gate::dbg_info(); }

IMPLEMENT
unsigned
Ipc_gate::log_fmt(Tb_entry *e, int max, char *buf)
{
  Log_ipc_gate_invoke *l = e->payload<Log_ipc_gate_invoke>();
  return snprintf(buf, max, "D-gate=%lx D-thread=%lx L=%lx",
                  l->gate_dbg_id, l->thread_dbg_id, l->label);
}

