/*
 * PPC page cache
 */
INTERFACE[ppc32]:


EXTENSION class Mem_space
{
private:
  Status v_insert_cache(Pte_base *e, Address virt, size_t size, unsigned page_attribs,
                        Dir_type *dir = 0);
  unsigned long v_delete_cache(Pt_entry *e, unsigned page_attribs);
};


//------------------------------------------------------------------------------
IMPLEMENTATION[ppc32]:

IMPLEMENT
Mem_space::Status
Mem_space::v_insert_cache(Pte_base *e, Address virt, size_t size,
                          unsigned page_attribs, Dir_type *dir = 0)
{
  if(!dir) dir = _dir;

  Pdir::Iter i = dir->alloc_cast<Mem_space_q_alloc>()
    ->walk(Addr(virt), Pdir::Depth,
           Mem_space_q_alloc(_quota, Mapped_allocator::allocator()));

  if (EXPECT_FALSE(!i.e->valid() && i.shift() != Config::PAGE_SHIFT))
    return Insert_err_nomem;

  Address i_phys;

  //get physical addresses
  Pte_htab::pte_lookup(i.e, &i_phys);

  if(i.e->valid() && e->addr() == i_phys)
    {
      Status state = pte_attrib_upgrade(i.e, size, page_attribs);
      return state;
    }

  *i.e = e->raw() | page_attribs;

  //if super-page, set Pse_bit in Pdir
  if(size == Config::SUPERPAGE_SIZE)
    {
      i = dir->walk(Addr(virt), Pdir::Super_level);
      *i.e = i.e->raw() | Pte_base::Pse_bit;
    }

  return Insert_ok;
}

IMPLEMENT
unsigned long
Mem_space::v_delete_cache(Pt_entry *e, unsigned page_attribs = Page_all_attribs)
{
  unsigned ret;

  ret = e->raw() & page_attribs;

  if (!(page_attribs & Page_user_accessible))
    // downgrade PDE (superpage) rights
    e->del_attr(page_attribs);
  else
    // delete PDE (superpage)
    e = 0;

  return ret;
}
