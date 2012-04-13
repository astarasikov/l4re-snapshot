INTERFACE:

#include "context.h"
#include "kobject.h"
#include "l4_types.h"
#include "rcupdate.h"
#include "space.h"
#include "spin_lock.h"

class slab_cache_anon;

/**
 * \brief A task is a protection domain.
 *
 * A is derived from Space, which aggregates a set of address spaces.
 * Additionally to a space, a task provides initialization and
 * destruction functionality for a protection domain.
 * Task is also derived from Rcu_item to provide RCU shutdown of tasks.
 */
class Task :
  public Kobject,
  public Space
{
  FIASCO_DECLARE_KOBJ();

  friend class Jdb_space;

private:
  /// \brief Do host (platform) specific initialization.
  void host_init();

  /// \brief Map the trace buffer to the user address space.
  void map_tbuf();

public:
  enum Operation
  {
    Map         = 0,
    Unmap       = 1,
    Cap_info    = 2,
    Add_ku_mem  = 3,
    Ldt_set_x86 = 0x11,
    Vm_ops      = 0x20,
  };

private:
  /// map the global utcb pointer page into this task
  void map_utcb_ptr_page();
};


//---------------------------------------------------------------------------
IMPLEMENTATION:

#include "atomic.h"
#include "auto_ptr.h"
#include "config.h"
#include "entry_frame.h"
#include "globals.h"
#include "kdb_ke.h"
#include "kmem.h"
#include "kmem_slab_simple.h"
#include "l4_types.h"
#include "l4_buf_iter.h"
#include "logdefs.h"
#include "map_util.h"
#include "mem_layout.h"
#include "ram_quota.h"
#include "paging.h"
#include "vmem_alloc.h"

FIASCO_DEFINE_KOBJ(Task);

static Kmem_slab_t<Task::Ku_mem> _k_u_mem_list_alloc("Ku_mem");
slab_cache_anon *Space::Ku_mem::a = &_k_u_mem_list_alloc;

extern "C" void vcpu_resume(Trap_state *, Return_frame *sp)
   FIASCO_FASTCALL FIASCO_NORETURN;

PUBLIC virtual
int
Task::resume_vcpu(Context *ctxt, Vcpu_state *vcpu, bool user_mode)
{
  Trap_state ts;
  memcpy(&ts, &vcpu->_ts, sizeof(Trap_state));

  assert_kdb(cpu_lock.test());

  ts.sanitize_user_state();

  // FIXME: UX is currently broken
  /* UX:ctxt->vcpu_resume_user_arch(); */
  if (user_mode)
    vcpu->state |= Vcpu_state::F_traps | Vcpu_state::F_exceptions
                   | Vcpu_state::F_debug_exc;

  ctxt->space_ref()->user_mode(user_mode);
  switchin_context(ctxt->space());
  vcpu_resume(&ts, ctxt->regs());
}

PUBLIC virtual
bool
Task::put()
{ return dec_ref() == 0; }

PRIVATE
int
Task::alloc_ku_mem_chunk(User<void>::Ptr u_addr, unsigned size, void **k_addr)
{
  assert_kdb ((size & (size - 1)) == 0);

  Mapped_allocator *const alloc = Mapped_allocator::allocator();
  void *p = alloc->q_unaligned_alloc(ram_quota(), size);

  if (EXPECT_FALSE(!p))
    return -L4_err::ENomem;

  // clean up utcbs
  memset(p, 0, size);

  unsigned long page_size = Config::PAGE_SIZE;

  // the following works because the size is a power of two
  // and once we have size larger than a super page we have
  // always multiples of superpages
  if (size >= Config::SUPERPAGE_SIZE)
    page_size = Config::SUPERPAGE_SIZE;

  for (unsigned long i = 0; i < size; i += page_size)
    {
      Address kern_va = (Address)p + i;
      Address user_va = (Address)u_addr.get() + i;
      Address pa = mem_space()->pmem_to_phys(kern_va);

      // must be valid physical address
      assert(pa != ~0UL);

      Mem_space::Status res =
	mem_space()->v_insert(Mem_space::Phys_addr(pa),
	    Mem_space::Addr(user_va), Mem_space::Size(page_size),
	    Mem_space::Page_writable | Mem_space::Page_user_accessible
	    | Mem_space::Page_cacheable);

      switch (res)
	{
	case Mem_space::Insert_ok: break;
	case Mem_space::Insert_err_nomem:
	  free_ku_mem_chunk(p, u_addr, size, i);
	  return -L4_err::ENomem;

	case Mem_space::Insert_err_exists:
	  free_ku_mem_chunk(p, u_addr, size, i);
	  return -L4_err::EExists;

	default:
	  printf("UTCB mapping failed: va=%p, ph=%p, res=%d\n",
	      (void*)user_va, (void*)kern_va, res);
	  kdb_ke("BUG in utcb allocation");
	  free_ku_mem_chunk(p, u_addr, size, i);
	  return 0;
	}
    }

  *k_addr = p;
  return 0;
}


PRIVATE
int
Task::alloc_ku_mem(L4_fpage ku_area)
{
  if (ku_area.order() < Config::PAGE_SHIFT || ku_area.order() > 20)
    return -L4_err::EInval;

  Mword sz = 1UL << ku_area.order();

  Ku_mem *m = new (ram_quota()) Ku_mem();

  if (!m)
    return -L4_err::ENomem;

  User<void>::Ptr u_addr((void*)Virt_addr(ku_area.mem_address()).value());

  void *p;
  if (int e = alloc_ku_mem_chunk(u_addr, sz, &p))
    {
      m->free(ram_quota());
      return e;
    }

  m->u_addr = u_addr;
  m->k_addr = p;
  m->size = sz;

  // safely add the new Ku_mem object to the list
  do
    {
      m->next = _ku_mem;
    }
  while (!mp_cas(&_ku_mem, m->next, m));

  return 0;
}

PRIVATE inline NOEXPORT
void
Task::free_ku_mem(Ku_mem *m)
{
  free_ku_mem_chunk(m->k_addr, m->u_addr, m->size, m->size);
  m->free(ram_quota());
}

PRIVATE
void
Task::free_ku_mem_chunk(void *k_addr, User<void>::Ptr u_addr, unsigned size,
                        unsigned mapped_size)
{

  Mapped_allocator * const alloc = Mapped_allocator::allocator();
  unsigned long page_size = Config::PAGE_SIZE;

  // the following works because the size is a poer of two
  // and once we have size larger than a super page we have
  // always multiples of superpages
  if (size >= Config::SUPERPAGE_SIZE)
    page_size = Config::SUPERPAGE_SIZE;

  for (unsigned long i = 0; i < mapped_size; i += page_size)
    {
      Address user_va = (Address)u_addr.get() + i;
      mem_space()->v_delete(Mem_space::Addr(user_va),
                            Mem_space::Size(page_size));
    }

  alloc->q_unaligned_free(ram_quota(), size, k_addr);
}

PRIVATE
void
Task::free_ku_mem()
{
  Ku_mem *m = _ku_mem;
  _ku_mem = 0;

  while (m)
    {
      Ku_mem *d = m;
      m = m->next;

      free_ku_mem(d);
    }
}


/** Allocate space for the UTCBs of all threads in this task.
 *  @ return true on success, false if not enough memory for the UTCBs
 */
PUBLIC
bool
Task::initialize()
{
  // For UX, map the UTCB pointer page. For ia32, do nothing
  map_utcb_ptr_page();

  return true;
}

/**
 * \brief Create a normal Task.
 * \pre \a parent must be valid and exist.
 */
PUBLIC
template< typename SPACE_FACTORY >
Task::Task(SPACE_FACTORY const &sf, Ram_quota *q)
  : Space(sf, q)
{
  host_init();

  // increment reference counter from zero
  inc_ref(true);

  if (mem_space()->is_sigma0())
    map_tbuf();
}

PROTECTED template<typename SPACE_FACTORY>
Task::Task(SPACE_FACTORY const &sf, Ram_quota *q, Mem_space::Dir_type* pdir)
  : Space(sf, q, pdir)
{
  // increment reference counter from zero
  inc_ref(true);
}

// The allocator for tasks
static Kmem_slab_t<Task> _task_allocator("Task");

PROTECTED static
slab_cache_anon*
Task::allocator()
{ return &_task_allocator; }


PROTECTED inline NEEDS["kmem_slab_simple.h"]
void *
Task::operator new (size_t size, void *p)
{
  (void)size;
  assert (size == sizeof (Task));
  return p;
}


PUBLIC //inline NEEDS["kmem_slab_simple.h"]
void
Task::operator delete (void *ptr)
{
  Task *t = reinterpret_cast<Task*>(ptr);
  LOG_TRACE("Kobject delete", "del", current(), __fmt_kobj_destroy,
            Log_destroy *l = tbe->payload<Log_destroy>();
            l->id = t->dbg_id();
            l->obj = t;
            l->type = "Task";
            l->ram = t->ram_quota()->current());

  allocator()->q_free(t->ram_quota(), ptr);
}


PUBLIC template< typename SPACE_FACTORY >
static
Task *
Task::create(SPACE_FACTORY const &sf, Ram_quota *quota,
             L4_fpage const &utcb_area)
{
  void *t = allocator()->q_alloc(quota);
  if (!t)
    return 0;

  auto_ptr<Task> a(new (t) Task(sf, quota));
  if (!a->valid())
    return 0;

  if (utcb_area.is_valid())
    {
      int e = a->alloc_ku_mem(utcb_area);
      if (e < 0)
        return 0;
    }

  return a.release();
}

PUBLIC inline
bool
Task::valid() const
{ return mem_space()->valid(); }


/**
 * \brief Shutdown the task.
 *
 * Currently:
 * -# Unbind and delete all contexts bound to this task.
 * -# Unmap everything from all spaces.
 * -# Delete child tasks.
 */
PUBLIC
void
Task::destroy(Kobject ***reap_list)
{
  Kobject::destroy(reap_list);

  fpage_unmap(this, L4_fpage::all_spaces(L4_fpage::RWX), L4_map_mask::full(), reap_list);
}

PRIVATE inline NOEXPORT
L4_msg_tag
Task::sys_map(unsigned char rights, Syscall_frame *f, Utcb *utcb)
{
  LOG_TRACE("Task map", "map", ::current(), __task_unmap_fmt,
      Log_unmap *lu = tbe->payload<Log_unmap>();
      lu->id = dbg_id();
      lu->mask  = utcb->values[1];
      lu->fpage = utcb->values[2]);

  if (EXPECT_FALSE(!(rights & L4_fpage::W)))
    return commit_result(-L4_err::EPerm);

  L4_msg_tag const tag = f->tag();

  Obj_space *s = current()->space()->obj_space();
  L4_snd_item_iter snd_items(utcb, tag.words());

  if (EXPECT_FALSE(!tag.items() || !snd_items.next()))
    return commit_result(-L4_err::EInval);

  L4_fpage src_task(snd_items.get()->d);
  if (EXPECT_FALSE(!src_task.is_objpage()))
    return commit_result(-L4_err::EInval);

  Task *from = Kobject::dcast<Task*>(s->lookup_local(src_task.obj_index()));
  if (!from)
    return commit_result(-L4_err::EInval);

  Reap_list rl;
  L4_error ret;

    {
      // enforce lock order to prevent deadlocks.
      // always take lock from task with the lower memory address first
      Lock_guard_2<Lock> guard;

      // FIXME: avoid locking the current task, it is not needed
      if (!guard.lock(&existence_lock, &from->existence_lock))
        return commit_result(-L4_err::EInval);

      cpu_lock.clear();

      ret = fpage_map(from, L4_fpage(utcb->values[2]), this,
                      L4_fpage::all_spaces(), L4_msg_item(utcb->values[1]), &rl);
      cpu_lock.lock();
    }

  cpu_lock.clear();
  rl.del();
  cpu_lock.lock();

  // FIXME: treat reaped stuff
  if (ret.ok())
    return commit_result(0);
  else
    return commit_error(utcb, ret);
}


PRIVATE inline NOEXPORT
L4_msg_tag
Task::sys_unmap(Syscall_frame *f, Utcb *utcb)
{
  Reap_list rl;
  unsigned words = f->tag().words();

  LOG_TRACE("Task unmap", "unm", ::current(), __task_unmap_fmt,
            Log_unmap *lu = tbe->payload<Log_unmap>();
            lu->id = dbg_id();
            lu->mask  = utcb->values[1];
            lu->fpage = utcb->values[2]);

    {
      Lock_guard<Lock> guard;

      // FIXME: avoid locking the current task, it is not needed
      if (!guard.try_lock(&existence_lock))
        return commit_error(utcb, L4_error::Not_existent);

      cpu_lock.clear();

      L4_map_mask m(utcb->values[1]);

      for (unsigned i = 2; i < words; ++i)
        {
          unsigned const flushed = fpage_unmap(this, L4_fpage(utcb->values[i]), m, rl.list());
          utcb->values[i] = (utcb->values[i] & ~0xfUL) | flushed;
        }
      cpu_lock.lock();
    }

  cpu_lock.clear();
  rl.del();
  cpu_lock.lock();

  return commit_result(0, words);
}

PRIVATE inline NOEXPORT
L4_msg_tag
Task::sys_cap_valid(Syscall_frame *, Utcb *utcb)
{
  L4_obj_ref obj(utcb->values[1]);

  if (obj.special())
    return commit_result(0);

  Obj_space::Capability cap = obj_space()->lookup(obj.cap());
  if (EXPECT_TRUE(cap.valid()))
    {
      if (!(utcb->values[1] & 1))
	return commit_result(1);
      else
	return commit_result(cap.obj()->map_root()->cap_ref_cnt());
    }
  else
    return commit_result(0);
}

PRIVATE inline NOEXPORT
L4_msg_tag
Task::sys_caps_equal(Syscall_frame *, Utcb *utcb)
{
  L4_obj_ref obj_a(utcb->values[1]);
  L4_obj_ref obj_b(utcb->values[2]);

  if (obj_a == obj_b)
    return commit_result(1);

  if (obj_a.special() || obj_b.special())
    return commit_result(obj_a.special_cap() == obj_b.special_cap());

  Obj_space::Capability c_a = obj_space()->lookup(obj_a.cap());
  Obj_space::Capability c_b = obj_space()->lookup(obj_b.cap());

  return commit_result(c_a == c_b);
}

PRIVATE inline NOEXPORT
L4_msg_tag
Task::sys_add_ku_mem(Syscall_frame *f, Utcb *utcb)
{
  unsigned const w = f->tag().words();
  for (unsigned i = 1; i < w; ++i)
    {
      L4_fpage ku_fp(utcb->values[i]);
      if (!ku_fp.is_valid() || !ku_fp.is_mempage())
	return commit_result(-L4_err::EInval);

      int e = alloc_ku_mem(ku_fp);
      if (e < 0)
	return commit_result(e);
    }

  return commit_result(0);
}

PRIVATE inline NOEXPORT
L4_msg_tag
Task::sys_cap_info(Syscall_frame *f, Utcb *utcb)
{
  L4_msg_tag const &tag = f->tag();

  switch (tag.words())
    {
    default: return commit_result(-L4_err::EInval);
    case 2:  return sys_cap_valid(f, utcb);
    case 3:  return sys_caps_equal(f, utcb);
    }
}


PUBLIC
void
Task::invoke(L4_obj_ref, Mword rights, Syscall_frame *f, Utcb *utcb)
{
  if (EXPECT_FALSE(f->tag().proto() != L4_msg_tag::Label_task))
    {
      f->tag(commit_result(-L4_err::EBadproto));
      return;
    }

  switch (utcb->values[0])
    {
    case Map:
      f->tag(sys_map(rights, f, utcb));
      return;
    case Unmap:
      f->tag(sys_unmap(f, utcb));
      return;
    case Cap_info:
      f->tag(sys_cap_info(f, utcb));
      return;
    case Add_ku_mem:
      f->tag(sys_add_ku_mem(f, utcb));
      return;
    default:
      L4_msg_tag tag = f->tag();
      if (invoke_arch(tag, utcb))
	f->tag(tag);
      else
        f->tag(commit_result(-L4_err::ENosys));
      return;
    }
}


//---------------------------------------------------------------------------
IMPLEMENTATION [!ux]:

IMPLEMENT inline
void
Task::map_utcb_ptr_page()
{}

IMPLEMENT inline
void
Task::host_init()
{}

IMPLEMENT inline
void
Task::map_tbuf()
{}

PUBLIC inline
Task::~Task()
{ free_ku_mem(); }


// ---------------------------------------------------------------------------
INTERFACE [debug]:

EXTENSION class Task
{
private:
  struct Log_unmap
  {
    Mword id;
    Mword mask;
    Mword fpage;
  } __attribute__((packed));

  static unsigned unmap_fmt(Tb_entry *, int max, char *buf) asm ("__task_unmap_fmt");
};

// ---------------------------------------------------------------------------
IMPLEMENTATION [debug]:

IMPLEMENT
unsigned
Task::unmap_fmt(Tb_entry *e, int max, char *buf)
{
  Log_unmap *l = e->payload<Log_unmap>();
  L4_fpage fp(l->fpage);
  return snprintf(buf, max, "task=[U:%lx] mask=%lx fpage=[%u/%u]%lx",
                  l->id, l->mask, (unsigned)fp.order(), fp.type(), l->fpage);
}

// ---------------------------------------------------------------------------
IMPLEMENTATION[!ia32 || !svm]:

PRIVATE inline NOEXPORT
L4_msg_tag
Task::sys_vm_run(Syscall_frame *, Utcb *)
{
  return commit_result(-L4_err::ENosys);
}

