IMPLEMENTATION:

#include <cstdio>

#include "config.h"
#include "jdb.h"
#include "jdb_screen.h"
#include "jdb_table.h"
#include "jdb_kobject.h"
#include "kernel_console.h"
#include "kmem.h"
#include "keycodes.h"
#include "space.h"
#include "task.h"
#include "thread_object.h"
#include "static_init.h"
#include "types.h"


class Jdb_obj_space : public Jdb_table, public Jdb_kobject_handler
{
public:
  enum Mode
  {
    Name,
    Raw,
    End_mode
  };

private:
  Address _base;
  Space  *_task;
  int     _level;
  Mode    _mode;

  bool show_kobject(Kobject_common *, int) { return false; }

};

static inline
Jdb_obj_space::Mode
operator ++ (Jdb_obj_space::Mode &m)
{
  long _m = m;
  ++_m;
  if (_m >= Jdb_obj_space::End_mode)
    _m = 0;

  m = Jdb_obj_space::Mode(_m);

  return m;
}

//char Jdb_obj_space_m::first_char;

PUBLIC
Jdb_obj_space::Jdb_obj_space(Address base = 0, int level = 0)
: Jdb_kobject_handler(0),
  _base(base),
  _task(0),
  _level(level),
  _mode(Name)
{
  Jdb_kobject::module()->register_handler(this);
}

PUBLIC
unsigned
Jdb_obj_space::col_width(unsigned column) const
{
  if (column == 0)
    return Jdb_screen::Col_head_size;
  else
    return 16;
}

PUBLIC
unsigned long
Jdb_obj_space::cols() const
{
  return 5;
}

PUBLIC
unsigned long
Jdb_obj_space::rows() const
{ return Obj_space::Map_max_address / (cols()-1); }

PUBLIC
void
Jdb_obj_space::print_statline(unsigned long row, unsigned long col)
{
  static char buf[128];
  unsigned rights;

  Kobject_iface *o = item(index(row,col), &rights);
  if (!o)
    {
      Jdb::printf_statline("objs", "<Space>=mode", "%lx: -- INVALID --",
                           index(row,col));
      return;
    }

  unsigned len = Jdb_kobject::obj_description(buf, sizeof(buf), true, o);
  Jdb::printf_statline("objs", "<Space>=mode",
		       "%lx: %-*s", index(row,col), len, buf);
}

PUBLIC
void
Jdb_obj_space::print_entry(Address entry)
{
  unsigned rights;
  Kobject_iface *o = item(entry, &rights);

  if (!o)
    printf("       --       ");
  else
    {
      char r = '-';
      switch (_mode)
	{
	case Name:
	  switch (rights)
	    {
	    case L4_fpage::WX: r = '*'; break;
	    case L4_fpage::W:  r = 'w'; break;
	    case L4_fpage::X:  r = 'x'; break;
	    }
	  printf("%05lx%c %-*s", o->dbg_info()->dbg_id(), r, 9, Jdb_kobject::kobject_type(o));
	  break;
	case Raw:
	default:
	  printf("%16lx", Mword(o) | rights);
	  break;
	}
    }
}

PUBLIC
void
Jdb_obj_space::draw_entry(unsigned long row, unsigned long col)
{
  if (col==0)
    printf("%06lx ", index(row, 1));
  else
    print_entry(index(row, col));
}

PRIVATE
Address
Jdb_obj_space::index(unsigned long row, unsigned long col)
{
  Mword e = (col-1) + (row * (cols()-1));
  return _base + e;
}

PRIVATE
bool
Jdb_obj_space::handle_user_keys(int c, Kobject_iface *o)
{
  if (!o)
    return false;

  Jdb_kobject_handler *h = Jdb_kobject::module()->first_global_handler();
  bool handled = false;
  while (h)
    {
      handled |= h->handle_key(o, c);
      h = h->next_global();
    }

  h = Jdb_kobject::module()->find_handler(o);
  if (h)
    handled |= h->handle_key(o, c);

  return handled;
}


PUBLIC
unsigned
Jdb_obj_space::key_pressed(int c, unsigned long &row, unsigned long &col)
{
  switch (c)
    {
    default:
      {
        unsigned rights;
        if (handle_user_keys(c, item(index(row, col), &rights)))
          return Redraw;
        return Nothing;
      }

    case KEY_CURSOR_HOME: // return to previous or go home
      return Back;

    case ' ':
      ++_mode;
      return Redraw;
    }
}

PUBLIC
bool
Jdb_obj_space::handle_key(Kobject_common *o, int code)
{
  if (code != 'o')
    return false;

  Space *t = Kobject::dcast<Task*>(o);
  if (!t)
    {
      Thread *th = Kobject::dcast<Thread_object *>(o);
      if (!th || !th->space())
	return false;

      t = th->space();
    }

  _task = t;
  show(0,0);

  return true;
}

static Jdb_obj_space jdb_obj_space INIT_PRIORITY(JDB_MODULE_INIT_PRIO);

// ------------------------------------------------------------------------
IMPLEMENTATION [obj_space_virt]:

PUBLIC
Kobject_iface *
Jdb_obj_space::item(Address entry, unsigned *rights)
{
  Mword dummy;
  Obj_space::Capability *c = _task->obj_space()->cap_virt(entry);
  if (!c)
    return 0;

  Mword mapped = Jdb::peek((Mword*)c, _task, dummy);

  if (!mapped)
    return 0;

  Kobject_iface *o = (Kobject_iface*)(dummy & ~3UL);
  *rights = dummy & 3;

  return o;
}

// ------------------------------------------------------------------------
IMPLEMENTATION [obj_space_phys]:
PUBLIC
Kobject_iface *
Jdb_obj_space::item(Address entry, unsigned *rights)
{
  Obj_space::Capability *c = _task->obj_space()->get_cap(entry);

  if (!c)
    return 0;

  Kobject_iface *o = c->obj();
  *rights = c->rights();

  return o;
}
