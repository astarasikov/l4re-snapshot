INTERFACE:

#include "types.h"

class Outer_cache
{
public:
  static void invalidate();
  static void invalidate(Address phys, bool sync = true);
  static void invalidate(Address start_phys, Address end_phys, bool do_sync = true);

  static void clean();
  static void clean(Address phys, bool do_sync = true);
  static void clean(Address start_phys, Address end_phys, bool do_sync = true);

  static void flush();
  static void flush(Address phys, bool do_sync = true);
  static void flush(Address start_phys, Address end_phys, bool do_sync = true);

  static void sync();
};

// ------------------------------------------------------------------------
INTERFACE [!outer_cache]:

EXTENSION class Outer_cache
{
public:
  enum
  {
    Cache_line_mask = 0,
    Cache_line_size = ~0,
  };
};

// ------------------------------------------------------------------------
IMPLEMENTATION [!outer_cache]:

IMPLEMENT inline
void
Outer_cache::invalidate()
{}

IMPLEMENT inline
void
Outer_cache::invalidate(Address, bool)
{}

IMPLEMENT inline
void
Outer_cache::clean()
{}

IMPLEMENT inline
void
Outer_cache::clean(Address, bool)
{}

IMPLEMENT inline
void
Outer_cache::flush()
{}

IMPLEMENT inline
void
Outer_cache::flush(Address, bool)
{}

IMPLEMENT inline
void
Outer_cache::sync() {}

// ------------------------------------------------------------------------
IMPLEMENTATION:

IMPLEMENT inline
void
Outer_cache::invalidate(Address start, Address end, bool do_sync)
{
  if (start & Cache_line_mask)
    flush(start, false);
  if (end & Cache_line_mask)
    flush(start, false);

  for (Address a = start & Cache_line_mask;
       a < end; a += Cache_line_size)
    invalidate(a, false);

  if (do_sync)
    sync();
}

IMPLEMENT inline
void
Outer_cache::clean(Address start, Address end, bool do_sync)
{
  for (Address a = start & Cache_line_mask;
       a < end; a += Cache_line_size)
    clean(a, false);
  if (do_sync)
    sync();
}

IMPLEMENT inline
void
Outer_cache::flush(Address start, Address end, bool do_sync)
{
  for (Address a = start & Cache_line_mask;
       a < end; a += Cache_line_size)
    flush(a, false);
  if (do_sync)
    sync();
}
