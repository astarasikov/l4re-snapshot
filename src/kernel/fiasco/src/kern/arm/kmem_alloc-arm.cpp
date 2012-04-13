IMPLEMENTATION [arm]:

#include "mem_unit.h"
#include "kmem_space.h"
#include "pagetable.h"
#include "ram_quota.h"

PRIVATE //inline
bool
Kmem_alloc::map_pmem(unsigned long phy, unsigned long size)
{
  static unsigned long next_map = Mem_layout::Map_base + (4 << 20);
  size = Mem_layout::round_superpage(size + (phy & ~Config::SUPERPAGE_MASK));
  phy = Mem_layout::trunc_superpage(phy);

  if (next_map + size > Mem_layout::Map_end)
    return false;

  for (unsigned long i = 0; i <size; i+=Config::SUPERPAGE_SIZE)
    {
      Pte pte = Kmem_space::kdir()->walk((char*)next_map+i,
	  Config::SUPERPAGE_SIZE, false, Ram_quota::root);
      pte.set(phy+i, Config::SUPERPAGE_SIZE, 
	  Mem_page_attr(Page::KERN_RW | Page::CACHEABLE),
	  true);

    }
  Mem_layout::add_pmem(phy, next_map, size);
  next_map += size;
  return true;
}

IMPLEMENT
Kmem_alloc::Kmem_alloc()
{
  Mword alloc_size = Config::KMEM_SIZE;
  a->init(Mem_layout::Map_base);
  Mem_region_map<64> map;
  /*unsigned long available_size =*/ create_free_map(Kip::k(), &map);

  for (int i = map.length() - 1; i >= 0 && alloc_size > 0; --i)
    {
      Mem_region f = map[i];
      if (f.size() > alloc_size)
	f.start += (f.size() - alloc_size);

      Kip::k()->add_mem_region(Mem_desc(f.start, f.end,
	    Mem_desc::Reserved));
      //printf("ALLOC1: [%08lx; %08lx] sz=%ld\n", f.start, f.end, f.size());
      if (Mem_layout::phys_to_pmem(f.start) == ~0UL)
	if (!map_pmem(f.start, f.size()))
	  panic("Kmem_alloc::Kmem_alloc(): cannot map physical memory %p\n", (void*)f.start);
      a->add_mem((void*)Mem_layout::phys_to_pmem(f.start), f.size());
      alloc_size -= f.size();
    }

  if (alloc_size)
    printf("Kmem_alloc::Kmem_alloc(): cannot allocate sufficient kernel memory\n");
}

//----------------------------------------------------------------------------
IMPLEMENTATION [arm && debug]:

#include <cstdio>

#include "kip_init.h"
#include "panic.h"

PUBLIC
void Kmem_alloc::debug_dump()
{
  a->dump();

  unsigned long free = a->avail();
  printf("Used %ldKB out of %dKB of Kmem\n",
	 (Config::KMEM_SIZE - free + 1023)/1024,
	 (Config::KMEM_SIZE        + 1023)/1024);
}
