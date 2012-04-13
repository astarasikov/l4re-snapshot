INTERFACE:

#include "types.h"
#include "mapping.h"
#include "pages.h"

class Mapping_tree;		// forward decls
class Physframe;
class Treemap;
class Space;

/** A mapping database.
 */
class Mapdb
{
  friend class Jdb_mapdb;

public:
  typedef ::Mapping Mapping;
  class Frame;
  /** Iterator for mapping trees.  This iterator knows how to descend
    into Treemap nodes.
   */
  class Iterator
  {
    Mapping_tree* _mapping_tree;
    Mapping* _parent;
    Mapping* _cursor;
    size_t   _page_shift;
    Treemap *_submap;
    Physframe *_subframe;
    Page_number _submap_index;
    Page_number _offs_begin, _offs_end;

  public:
    inline Mapping* operator->() const { return _cursor; }
    inline operator Mapping*() const { return _cursor; }
    inline Iterator() : _submap (0), _submap_index(Page_number::create(0)) {}
    inline Iterator(const Mapdb::Frame& f, Mapping* parent,
                    Page_number va_begin, Page_number va_end);
    inline ~Iterator();
    inline void neutralize();
    inline Page_count size() const;
    inline size_t shift() const { return _page_shift; }
    inline Page_number page() const
    { return Page_number(_cursor->page() << shift()); }

    inline Iterator &operator++ ();
  };

  // TYPES
  class Frame 
  {
    friend class Mapdb;
    friend class Mapdb::Iterator;
    Treemap* treemap;
    Physframe* frame;

  public:
    inline Page_number vaddr(Mapping* m) const;
  };

private:
  // DATA
  Treemap* const _treemap;
};


IMPLEMENTATION:

/* The mapping database.

 * This implementation encodes mapping trees in very compact arrays of
 * fixed sizes, prefixed by a tree header (Mapping_tree).  Array
 * sizes can vary from 4 mappings to 4<<15 mappings.  For each size,
 * we set up a slab allocator.  To grow or shrink the size of an
 * array, we have to allocate a larger or smaller tree from the
 * corresponding allocator and then copy the array elements.
 * 
 * The array elements (Mapping) contain a tree depth element.  This
 * depth and the relative position in the array is all information we
 * need to derive tree structure information.  Here is an example:
 * 
 * array
 * element   depth
 * number    value    comment
 * --------------------------
 * 0         0        Sigma0 mapping
 * 1         1        child of element #0 with depth 0
 * 2         2        child of element #1 with depth 1
 * 3         2        child of element #1 with depth 1
 * 4         3        child of element #3 with depth 2
 * 5         2        child of element #1 with depth 1
 * 6         3        child of element #5 with depth 2
 * 7         1        child of element #0 with depth 0
 * 
 * This array is a pre-order encoding of the following tree:
 * 
 *                   0
 * 	          /     \ 
 *               1       7
 *            /  |  \                   
 *           2   3   5
 *               |   |
 *        	 4   6
       	       	   
 * The mapping database (Mapdb) is organized in a hierarchy of
 * frame-number-keyed maps of Mapping_trees (Treemap).  The top-level
 * Treemap contains mapping trees for superpages.  These mapping trees
 * may contain references to Treemaps for subpages.  (Original credits
 * for this idea: Christan Szmajda.)
 *
 *        Treemap
 *        -------------------------------
 *     	  | | | | | | | | | | | | | | | | array of ptr to 4M Mapping_tree's
 *        ---|---------------------------
 *           |
 *           v a Mapping_tree
 *           ---------------
 *           |             | tree header
 *           |-------------|
 *           |             | 4M Mapping *or* ptr to nested Treemap
 *           |             | e.g.
 *           |      ----------------| Treemap
 *           |             |        v array of ptr to 4K Mapping_tree's
 *           ---------------        -------------------------------
 *                                  | | | | | | | | | | | | | | | |
 *                                  ---|---------------------------
 *                                     |
 *                                     v a Mapping_tree
 *                             	       ---------------
 *                                     |             | tree header
 *                                     |-------------|
 *                                     |             | 4K Mapping
 *                                     |             |
 *                                     |             |
 *                                     |             |
 *                                     ---------------

 * IDEAS for enhancing this implementation: 

 * We often have to find a tree header corresponding to a mapping.
 * Currently, we do this by iterating backwards over the array
 * containing the mappings until we find the Sigma0 mapping, from
 * whose address we can compute the address of the tree header.  If
 * this becomes a problem, we could add one more byte to the mappings
 * with a hint (negative array offset) where to find the sigma0
 * mapping.  (If the hint value overflows, just iterate using the hint
 * value of the mapping we find with the first hint value.)  Another
 * idea (from Adam) would be to just look up the tree header by using
 * the physical address from the page-table lookup, but we would need
 * to change the interface of the mapping database for that (pass in
 * the physical address at all times), or we would have to include the
 * physical address (or just the address of the tree header) in the
 * Mapdb-user-visible Mapping (which could be different from the
 * internal tree representation).  (XXX: Implementing one of these
 * ideas is probably worthwile doing!)

 * Instead of copying whole trees around when they grow or shrink a
 * lot, or copying parts of trees when inserting an element, we could
 * give up the array representation and add a "next" pointer to the
 * elements -- that is, keep the tree of mappings in a
 * pre-order-encoded singly-linked list (credits to: Christan Szmajda
 * and Adam Wiggins).  24 bits would probably be enough to encode that
 * pointer.  Disadvantages: Mapping entries would be larger, and the
 * cache-friendly space-locality of tree entries would be lost.
 */

#include <cassert>
#include <cstring>

#include <auto_ptr.h>

#include "config.h"
#include "globals.h"
#include "helping_lock.h"
#include "mapped_alloc.h"
#include "mapping_tree.h"
#include "paging.h"
#include "kmem_slab.h"
#include "ram_quota.h"
#include "std_macros.h"

// 
// class Physframe
// 

/** Array elements for holding frame-specific data. */
class Physframe : public Base_mappable
{
  friend class Mapdb;
  friend class Mapdb::Iterator;
  friend class Treemap;
  friend class Jdb_mapdb;
}; // struct Physframe

#if 0 // Optimization: do this using memset in Physframe::alloc()
inline
Physframe::Physframe ()
{}

inline
void *
Physframe::operator new (size_t, void *p) 
{ return p; }
#endif

static inline
Physframe *
Physframe::alloc(Ram_quota *q, size_t size)
{
  unsigned long ps = (size*sizeof(Physframe) + 1023) & ~1023;

  if (!q->alloc(ps))
    return 0;

  Physframe* block
    = (Physframe*)Mapped_allocator::allocator()->unaligned_alloc(ps);
  
#if 1				// Optimization: See constructor
  if (block) 
    memset(block, 0, size * sizeof(Physframe));
  else
    q->free(ps);
#else
  assert (block);
  for (unsigned i = 0; i < size; ++i)
    new (block + i) Physframe();
#endif
  
  return block;
}

inline NOEXPORT 
       NEEDS["mapping_tree.h", Treemap::~Treemap, Treemap::operator delete]
Physframe::~Physframe()
{
  if (tree.get())
    {
      Lock_guard <Base_mappable::Lock> guard(&lock);

      // Find next-level trees.
      for (Mapping* m = tree->mappings();
	   m && !m->is_end_tag();
	   m = tree->next(m))
	{
	  if (m->submap())
	    delete m->submap();
	}

      tree.reset();
    }
}

static // inline NEEDS[Physframe::~Physframe]
void 
Physframe::free(Ram_quota *q, Physframe *block, size_t size)
{
  for (unsigned i = 0; i < size; ++i)
    block[i].~Physframe();
  
  size = (size*sizeof(Physframe) + 1023) & ~1023;
  Mapped_allocator::allocator()->unaligned_free(size, block);
  q->free(size);
}

// 
// class Treemap
// 

class Treemap
{
  friend class Jdb_mapdb;
  friend class Treemap_ops;

  // DATA
  Page_count _key_end;		///< Number of Physframe entries
  Space *_owner_id;	///< ID of owner of mapping trees 
  Page_number _page_offset;	///< Virt. page address in owner's addr space
  size_t _page_shift;		///< Page size of mapping trees
  Physframe* _physframe;	///< Pointer to Physframe array
  const size_t* const _sub_shifts; ///< Pointer to array of sub-page sizes
  const unsigned _sub_shifts_max;  ///< Number of valid _page_sizes entries

  friend class Mapdb::Iterator;
  Mapdb::Iterator _unwind;	///< Chaining information for Mapdb_iterator
};

//
// class Treemap_ops
//
// Helper operations for Treemaps (used in Mapping_tree::flush)

class Treemap_ops
{
  unsigned long _page_shift;
};

PUBLIC inline
Treemap_ops::Treemap_ops(unsigned long page_shift)
: _page_shift(page_shift)
{}

PUBLIC 
unsigned long
Treemap_ops::mem_size(Treemap const *submap) const
{
  unsigned long quota 
    = submap->_key_end.value() * sizeof(Physframe) + sizeof(Treemap);

  for (Page_number key = Page_number::create(0);
      key < submap->_key_end;
      ++key)
    {
      Physframe* subframe = submap->frame(key);
      if (subframe->tree.get())
	quota += sizeof(Mapping);
    }

  return quota;
}

PUBLIC
void
Treemap_ops::grant(Treemap *submap, Space *new_space, Page_number va) const
{
  submap->_owner_id = new_space;
  submap->_page_offset = va << _page_shift;

  for (Page_number key = Page_number::create(0);
      key < submap->_key_end;
      ++key)
    {
      Physframe* subframe = submap->frame(key);
      if (Mapping_tree* tree = subframe->tree.get())
	{
	  Lock_guard<Base_mappable::Lock> guard(&subframe->lock);
	  tree->mappings()->set_space(new_space);
	  tree->mappings()->set_page(va + (key >> (_page_shift - submap->_page_shift)));
	}
    }
}

PUBLIC inline
Space *
Treemap_ops::owner(Treemap const *submap) const
{ return submap->owner(); }

PUBLIC inline
bool
Treemap_ops::is_partial(Treemap const *submap, Page_count offs_begin,
                        Page_count offs_end) const
{
  return offs_begin > Page_count::create(0)
    || offs_end < (submap->_key_end << submap->_page_shift);
}

PUBLIC inline
void 
Treemap_ops::del(Treemap *submap) const
{ delete submap; }

PUBLIC inline
void
Treemap_ops::flush(Treemap *submap,
                   Page_number offs_begin, Page_number offs_end) const
{
  for (Page_number i = offs_begin >> submap->_page_shift;
      i < (offs_end + Page_count::create((1UL << submap->_page_shift) - 1)
	>> submap->_page_shift);
      i++)
    {
      Page_count page_offs_begin = i << submap->_page_shift;
      Page_count page_offs_end = page_offs_begin;
      page_offs_end += Page_count::create(1UL << submap->_page_shift);

      Physframe* subframe = submap->frame(i);

      Lock_guard <Base_mappable::Lock> guard(&subframe->lock);

      if (offs_begin <= page_offs_begin && offs_end >= page_offs_end)
	subframe->tree.reset();
      else
	{
	  Page_count psz = Page_count::create(1UL << _page_shift);
	  submap->flush(subframe, subframe->tree->mappings(), false,
	    page_offs_begin > offs_begin
	    ? Page_number::create(0) : offs_begin.offset(psz),
	    page_offs_end < offs_end
	    ? psz
	    : (offs_end - Page_count::create(1)).offset(psz)
	      + Page_count::create(1));
	}
    }
}


//
// Treemap members
//

PUBLIC
Treemap::Treemap(Page_number key_end, Space *owner_id,
                 Page_number page_offset, size_t page_shift,
                 const size_t* sub_shifts, unsigned sub_shifts_max)
  : _key_end(key_end),
    _owner_id(owner_id),
    _page_offset(page_offset),
    _page_shift(page_shift),
    _physframe(Physframe::alloc(Mapping_tree::quota(owner_id),
               key_end.value())),
    _sub_shifts(sub_shifts),
    _sub_shifts_max(sub_shifts_max)
{
  assert (_physframe);
}

PUBLIC
inline NEEDS[Physframe::free, Mapdb] //::Iterator::neutralize]
Treemap::~Treemap()
{
  Physframe::free(Mapping_tree::quota(_owner_id),
      _physframe, _key_end.value());

  // Make sure that _unwind.~Mapdb_iterator is harmless: Reinitialize it.
  _unwind.neutralize();
}

static Kmem_slab_t<Treemap> _treemap_allocator("Treemap");

static
slab_cache_anon*
Treemap::allocator()
{ return &_treemap_allocator; }

PUBLIC
inline
void*
Treemap::operator new (size_t /*size*/, Ram_quota *q) throw()
{
  if (EXPECT_FALSE(!q->alloc(sizeof(Treemap))))
    return 0;

  void *m;
  if (EXPECT_TRUE(!!(m = allocator()->alloc())))
    return m;

  q->free(sizeof(Treemap));
  return 0;
}

PUBLIC
inline
void
Treemap::operator delete (void *block)
{
  Treemap *t = reinterpret_cast<Treemap*>(block);
  Space *id = t->_owner_id;
  allocator()->free(block);
  Mapping_tree::quota(id)->free(sizeof(Treemap));
}

PUBLIC inline
size_t
Treemap::page_shift() const
{
  return _page_shift;
}

PUBLIC inline
Space *
Treemap::owner() const
{
  return _owner_id;
}

PUBLIC inline
Page_number
Treemap::end_addr() const
{
  return (_key_end << _page_shift) + _page_offset;
}

PUBLIC inline NEEDS["paging.h"]
Page_number
Treemap::vaddr(Mapping* m) const
{
  return m->page() << _page_shift;
}

PUBLIC inline
void
Treemap::set_vaddr(Mapping* m, Page_number address) const
{
  m->set_page(address >> _page_shift);
}

PUBLIC
Physframe*
Treemap::tree(Page_number key)
{
  assert (key < _key_end);

  Physframe *f = &_physframe[key.value()];

  f->lock.lock();

  if (! f->tree.get())
    {
      if (!Mapping_tree::quota(_owner_id)->alloc(sizeof(Mapping)))
	{
	  f->lock.clear();
	  return 0;
	}

      auto_ptr<Mapping_tree> new_tree
	(new (0) Mapping_tree(0, key + (_page_offset >> _page_shift), 
	                      _owner_id));

      if (EXPECT_FALSE(!new_tree.get()))
	{
	  Mapping_tree::quota(_owner_id)->free(sizeof(Mapping));
	  f->lock.clear();
	  return 0;
	}

      f->tree = new_tree;
    }

  return f;
}

PUBLIC
Physframe*
Treemap::frame(Page_number key) const
{
  assert (key < _key_end);

  return &_physframe[key.value()];
}

PUBLIC
bool
Treemap::lookup(Page_number key, Space *search_space, Page_number search_va,
                Mapping** out_mapping, Treemap** out_treemap,
                Physframe** out_frame)
{
  // get and lock the tree.
  assert (key >> _page_shift < _key_end);
  Physframe *f = tree(key >> _page_shift); // returns locked frame
  assert (f);

  Mapping_tree *t = f->tree.get();
  Page_count const psz = Page_count::create(1UL << _page_shift);

  assert (t);
  assert (t->mappings()[0].space() == _owner_id);
  assert (vaddr(t->mappings()) == key.trunc(psz) + _page_offset);

  Mapping *m;
  bool ret = false;

  for (m = t->mappings(); m; m = t->next (m))
    {
      if (Treemap *sub = m->submap())
	{
	  // XXX Recursion.  The max. recursion depth should better be
	  // limited!
	  if ((ret = sub->lookup(key.offset(psz),
	                         search_space, search_va,
	                         out_mapping, out_treemap, out_frame)))
	    break;
	}
      else if (m->space() == search_space
	       && vaddr(m) == search_va.trunc(psz))
	{
	  *out_mapping = m;
	  *out_treemap = this;
	  *out_frame = f;
	  return true;		// found! -- return locked
	}
    }

  // not found, or found in submap -- unlock tree
  f->lock.clear();

  return ret;
}

PUBLIC
Mapping *
Treemap::insert(Physframe* frame, Mapping* parent, Space *space,
                Page_number va, Page_number phys, Page_count size)
{
  Mapping_tree* t = frame->tree.get();
  Treemap* submap = 0;
  Ram_quota *payer;
  Page_count const psz = Page_count::create(1UL << _page_shift);
  bool insert_submap = psz != size;

  // Inserting subpage mapping?  See if we can find a submap.  If so,
  // we don't have to allocate a new Mapping entry.
  if (insert_submap)
    {
      assert (psz > size);

      submap = t->find_submap(parent);
    }

  if (! submap) // Need allocation of new entry -- for submap or
		// normal mapping
    {
      // first check quota! In case of a new submap the parent pays for 
      // the node...
      payer = Mapping_tree::quota(insert_submap?parent->space():space);

      Mapping *free = t->allocate(payer, parent, insert_submap);
      if (EXPECT_FALSE(!free))
	return 0; 
      
      // Check for submap entry.
      if (! insert_submap)	// Not a submap entry
	{
	  free->set_space(space);
	  free->set_tag(0); // FIXME: support mapping tags
	  set_vaddr(free, va);

	  t->check_integrity(_owner_id);
	  return free;
	}

      assert (_sub_shifts_max > 0);

      submap = new (Mapping_tree::quota(parent->space())) 
	Treemap(Page_number::create(1UL << (_page_shift - _sub_shifts[0])),
	        parent->space(), vaddr(parent), _sub_shifts[0],
	        _sub_shifts + 1, _sub_shifts_max - 1);
      if (! submap)
	{
	  // free the mapping got with allocate
	  t->free_mapping(payer, free); 
	  return 0;
	}

      free->set_submap(submap);
    }

  Physframe* subframe = submap->tree(phys.offset(psz) >> submap->_page_shift);
  if (! subframe)
    return 0;

  Mapping_tree* subtree = subframe->tree.get();

  if (! subtree)
    return 0;

  // XXX recurse.
  Mapping* ret = submap->insert(subframe, subtree->mappings(), space, va, 
                                phys.offset(psz), size);
  submap->free(subframe);

  return ret;
} // Treemap::insert()

PUBLIC
void 
Treemap::free(Physframe* f)
{
  f->pack();
  f->tree.get()->check_integrity(_owner_id);

  // Unlock tree.
  f->lock.clear();
} // Treemap::free()


PUBLIC
void
Treemap::flush(Physframe* f, Mapping* parent, bool me_too,
               Page_number offs_begin, Page_number offs_end)
{
  assert (! parent->unused());

  // This is easy to do: We just have to iterate over the array
  // encoding the tree.
  Mapping_tree *t = f->tree.get();
  t->flush(parent, me_too, offs_begin, offs_end, Treemap_ops(_page_shift));

  t->check_integrity(_owner_id);
  return;
} // Treemap::flush()

PUBLIC
bool
Treemap::grant(Physframe* f, Mapping* m, Space *new_space, Page_number va)
{
  return f->tree.get()->grant(m, new_space, va >> _page_shift,
                              Treemap_ops(_page_shift));
}

//
// class Mapdb_iterator
//

/** Create a new mapping-tree iterator.  If parent is the result of a
    fresh lookup (and not the result of an insert operation), you can
    pass the corresponding Mapdb::Frame for optimization.
 */
IMPLEMENT inline NEEDS[Treemap::vaddr]
Mapdb::Iterator::Iterator(const Mapdb::Frame& f, Mapping* parent,
                          Page_number va_begin, Page_number va_end)
  : _mapping_tree(f.frame->tree.get()),
    _parent(parent),
    _cursor(parent),
    _page_shift(f.treemap->_page_shift),
    _submap(0),
    _subframe(0),
    _submap_index(Page_number::create(0))
{
  assert (_mapping_tree == Mapping_tree::head_of(parent));
  assert (! parent->submap());

  if (va_begin < f.treemap->vaddr(parent))
    va_begin = f.treemap->vaddr(parent);
  if (va_begin > va_end)
    va_begin = va_end;

  _offs_begin = va_begin - f.treemap->vaddr(parent);
  _offs_end = va_end - f.treemap->vaddr(parent);

  ++*this;
}

IMPLEMENT inline NEEDS[Physframe, <auto_ptr.h>, "helping_lock.h", Treemap]
Mapdb::Iterator::~Iterator()
{
  // unwind lock information
  while (_submap)
    {
      if (_subframe)
	_subframe->lock.clear();

      *this = _submap->_unwind;
    }
}

/** Make sure that the destructor does nothing.  Handy for scratch
    iterators such as Treemap::_unwind. */
IMPLEMENT inline
void
Mapdb::Iterator::neutralize()
{
  _submap = 0;
}

IMPLEMENT inline 
Page_count
Mapdb::Iterator::size() const
{
  return Page_count(1UL << _page_shift);
}

IMPLEMENT inline NEEDS ["mapping_tree.h"]
Mapdb::Iterator&
Mapdb::Iterator::operator++ ()
{
  for (;;)
    {
      _cursor = _mapping_tree->next_child(_parent, _cursor);
      
      if (_cursor && ! _cursor->submap())
	{			// Found a new regular child mapping.
	  if (_cursor)
	    return *this;
	}

      if (_cursor)		// _cursor is a submap
	{
	  Treemap* submap = _cursor->submap();
	  assert (submap);

	  // Before descending into array of subpages, save iterator
	  // state for later unwinding to this point.
	  submap->_unwind = *this;

	  // For subpages of original parent mapping, apply possible tag and
	  // virtual-address restrictions.  (Do not apply restrictions
	  // to other subpages because operations on a part of a
	  // superpage always affect *complete* child-superpages, as
	  // we do not support splitting out parts of superpages.
	  Page_count parent_size = submap->_key_end << submap->_page_shift;

	  if (_offs_end > parent_size)
	    _offs_end = parent_size;

	  if (_cursor->parent() == _parent)
	    {			// Submap of iteration root
	      _offs_begin = _offs_begin.offset(parent_size);
	      _offs_end = (_offs_end - Page_count::create(1)).offset(parent_size) + Page_count::create(1);
	    }
	  else			// Submap of a child mapping
	    {
	      _offs_begin = Page_number::create(0);
	      _offs_end = parent_size;
	    }

	  // Initialize rest of iterator for subframe iteration
	  _submap = submap;
	  _page_shift = _submap->_page_shift;
	  _subframe = 0;
	  _submap_index = _offs_begin >> _page_shift;
	  _mapping_tree = 0;
	  _parent = _cursor = 0;
	}

      else if (! _submap)	// End of iteration
	return *this;
	  
      // Clear previously acquired lock.
      if (_subframe)
	{
	  _subframe->lock.clear();
	  _subframe = 0;
	}

      // Find a new subframe.
      Physframe* f = 0;

      Page_count end_offs = _offs_end;
      end_offs += Page_count::create((1UL << _submap->_page_shift) - 1);
      end_offs >>= _submap->_page_shift;

      assert (end_offs <= _submap->_key_end);

      for (; _submap_index < end_offs;)
	{
	  f = _submap->frame(_submap_index++);
	  if (f->tree.get())
	    break;
	}
      
      if (f && f->tree.get())	// Found a subframe
	{
	  _subframe = f;
	  f->lock.lock();	// Lock it
	  _mapping_tree = f->tree.get();
	  _parent = _cursor = _mapping_tree->mappings();
	  continue;
	}
      
      // No new subframes found -- unwind to superpage tree
      *this = _submap->_unwind;
    }
}


// 
// class Mapdb
// 

/** Constructor.
    @param End physical end address of RAM.  (Used only if 
           Config::Mapdb_ram_only is true.) 
 */
PUBLIC
Mapdb::Mapdb(Space *owner, Page_number end_frame, const size_t* page_shifts,
             unsigned page_shifts_max)
: _treemap(new (Ram_quota::root) Treemap(end_frame, owner,
                                         Page_number::create(0),
                                         page_shifts[0], page_shifts + 1,
                                         page_shifts_max - 1))
{
  assert (_treemap);
} // Mapdb()

/** Destructor. */
PUBLIC
Mapdb::~Mapdb()
{
  delete _treemap;
}

/** Insert a new mapping entry with the given values as child of
    "parent".
    We assume that there is at least one free entry at the end of the
    array so that at least one insert() operation can succeed between a
    lookup()/free() pair of calls.  This is guaranteed by the free()
    operation which allocates a larger tree if the current one becomes
    to small.
    @param parent Parent mapping of the new mapping.
    @param space  Number of the address space into which the mapping is entered
    @param va     Virtual address of the mapped page.
    @param size   Size of the mapping.  For memory mappings, 4K or 4M.
    @return If successful, new mapping.  If out of memory or mapping 
           tree full, 0.
    @post  All Mapping* pointers pointing into this mapping tree,
           except "parent" and its parents, will be invalidated.
 */
PUBLIC
Mapping *
Mapdb::insert(const Mapdb::Frame& frame, Mapping* parent, Space *space,
              Page_number va, Page_number phys, Page_count size)
{
  return frame.treemap->insert(frame.frame, parent, space, va, phys, size);
} // insert()


/** 
 * Lookup a mapping and lock the corresponding mapping tree.  The returned
 * mapping pointer, and all other mapping pointers derived from it, remain
 * valid until free() is called on one of them.  We guarantee that at most 
 * one insert() operation succeeds between one lookup()/free() pair of calls 
 * (it succeeds unless the mapping tree is fu68,ll).
 * @param space Number of virtual address space in which the mapping 
 *              was entered
 * @param va    Virtual address of the mapping
 * @param phys  Physical address of the mapped pag frame
 * @return mapping, if found; otherwise, 0
 */
PUBLIC
bool
Mapdb::lookup(Space *space, Page_number va, Page_number phys,
             Mapping** out_mapping, Mapdb::Frame* out_lock)
{
  return _treemap->lookup(phys, space, va, out_mapping,
                          & out_lock->treemap, & out_lock->frame);
}

/** Unlock the mapping tree to which the mapping belongs.  Once a tree
    has been unlocked, all Mapping instances pointing into it become
    invalid.

    A mapping tree is locked during lookup().  When the tree is
    locked, the tree may be traversed (using member functions of
    Mapping, which serves as an iterator over the tree) or
    manipulated (using insert(), free(), flush(), grant()).  Note that
    only one insert() is allowed during each locking cycle.

    @param mapping_of_tree Any mapping belonging to a mapping tree.
 */
PUBLIC
static void
Mapdb::free(const Mapdb::Frame& f)
{
  f.treemap->free(f.frame);
} // free()

/** Delete mappings from a tree.  This function deletes mappings
    recusively.
    @param m Mapping that denotes the subtree that should be deleted.
    @param me_too If true, delete m as well; otherwise, delete only 
           submappings.
 */
PUBLIC static
void
Mapdb::flush(const Mapdb::Frame& f, Mapping *m, L4_map_mask mask,
             Page_number va_start, Page_number va_end)
{
  Page_count size = Page_count::create(1UL << f.treemap->page_shift());
  Page_number offs_begin = va_start > f.treemap->vaddr(m)
    ? va_start - f.treemap->vaddr(m) : Page_number::create(0);
  Page_number offs_end = va_end > f.treemap->vaddr(m) + size
    ? size : va_end - f.treemap->vaddr(m);

  f.treemap->flush(f.frame, m, mask.self_unmap(), offs_begin, offs_end);
} // flush()

/** Change ownership of a mapping.
    @param m Mapping to be modified.
    @param new_space Number of address space the mapping should be 
                     transferred to
    @param va Virtual address of the mapping in the new address space
 */
PUBLIC
bool
Mapdb::grant(const Mapdb::Frame& f, Mapping *m, Space *new_space,
             Page_number va)
{
  return f.treemap->grant(f.frame, m, new_space, va);
}

/** Return page size of given mapping and frame. */
PUBLIC static inline NEEDS[Treemap::page_shift]
size_t
Mapdb::shift(const Mapdb::Frame& f, Mapping * /*m*/)
{
  // XXX add special case for _mappings[0]: Return superpage size.
  return f.treemap->page_shift();
}

PUBLIC static inline NEEDS[Treemap::vaddr]
Page_number
Mapdb::vaddr(const Mapdb::Frame& f, Mapping* m)
{
  return f.treemap->vaddr(m);
}

// 
// class Mapdb::Frame
// 

IMPLEMENT inline NEEDS[Treemap::vaddr]
Page_number
Mapdb::Frame::vaddr(Mapping* m) const
{
  return treemap->vaddr(m);

}

PUBLIC inline NEEDS [Treemap::end_addr]
bool
Mapdb::valid_address(Page_number phys)
{
  return phys < _treemap->end_addr();
}


PUBLIC inline
Mapdb::Mapping *
Mapdb::check_for_upgrade(Page_number phys,
                         Space *from_id, Page_number snd_addr,
                         Space *to_id, Page_number rcv_addr,
                         Frame *mapdb_frame)
{
  // Check if we can upgrade mapping.  Otherwise, flush target
  // mapping.
  Mapping* receiver_mapping;
  if (valid_address(phys) // Can lookup in mapdb
      && lookup(to_id, rcv_addr, phys, &receiver_mapping, mapdb_frame))
    {
      Mapping* receiver_parent = receiver_mapping->parent();
      if (receiver_parent->space() == from_id
	  && vaddr(*mapdb_frame, receiver_parent) == snd_addr)
	return receiver_parent;
      else		// Not my child -- cannot upgrade
	free(*mapdb_frame);
    }
  return 0;
}

