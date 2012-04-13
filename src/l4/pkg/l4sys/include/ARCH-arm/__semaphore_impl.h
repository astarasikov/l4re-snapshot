/**
 * \file
 * \brief  User-lock implementation for ARM
 */
/*
 * (c) 2008-2009 Adam Lackorzynski <adam@os.inf.tu-dresden.de>,
 *               Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 *
 * As a special exception, you may use this file as part of a free software
 * library without restriction.  Specifically, if other files instantiate
 * templates or use macros or inline functions from this file, or you compile
 * this file and link it with other files to produce an executable, this
 * file does not by itself cause the resulting executable to be covered by
 * the GNU General Public License.  This exception does not however
 * invalidate any other reasons why the executable file might be covered by
 * the GNU General Public License.
 */
#pragma once

#include <l4/sys/utcb.h>
#include <l4/sys/atomic.h>
#include <l4/sys/compiler.h>
#include <l4/sys/ipc.h>

L4_INLINE l4_msgtag_t
l4_usem_down_to(l4_cap_idx_t lock, l4_u_semaphore_t *sem, l4_timeout_s timeout) L4_NOTHROW
{
  do
    {
      if (__builtin_expect(l4_atomic_add((long*)&(sem->counter), -1) >= 0, 1))
	return l4_msgtag(0, 0, 0, 0);

      l4_utcb_mr()->mr[0] = (l4_addr_t)sem;

      register unsigned long _lock    __asm__ ("r2") = lock | L4_SYSF_CALL;
      register unsigned long _timeout __asm__ ("r3") = timeout.t;
      register unsigned long _flags   __asm__ ("r4") = 0;
      register l4_msgtag_t   _tag     __asm__ ("r0") =
        l4_msgtag(0 /*L4_SEMAPHORE_DOWN*/, 1, 0, 0);

      __asm__ __volatile__
	("@ l4_usem_down_to (semaphore block) \n\t"
	 PIC_SAVE_ASM
	 "mov     lr, pc       \n\t"
	 "mov     pc, %[sys]   \n\t"
	 PIC_RESTORE_ASM
	 :
	 "=r" (_lock),
	 "=r" (_timeout),
	 "=r" (_flags),
	 "=r" (_tag.raw)
	 :
	 "0" (_lock),
	 "1" (_timeout),
	 "2" (_flags),
	 "3" (_tag.raw),
	 [sys] "i" (L4_SYSCALL_INVOKE)
	 :
	 "cc", "memory", "lr"
        );

      if (l4_msgtag_label(_tag) != L4_USEM_RETRY)
	{
	  l4_msgtag_t t;
          t.raw = _tag.raw;
	  return t;
	}
    }
  while (1);
}

L4_INLINE l4_msgtag_t
l4_usem_up(l4_cap_idx_t lock, l4_u_semaphore_t *sem) L4_NOTHROW
{
  l4_atomic_add((long*)&(sem->counter), 1);

  if (__builtin_expect(sem->flags == 0, 1))
    return l4_msgtag(0,0,0,0);

  l4_utcb_mr()->mr[0] = (l4_addr_t)sem;

  register unsigned long _lock    __asm__ ("r2") = lock | L4_SYSF_CALL;
  register unsigned long _timeout __asm__ ("r3") = 0;
  register unsigned long _flags   __asm__ ("r4") = 0;
  register unsigned long _tag     __asm__ ("r0") =
    l4_msgtag(1 /* L4_SEMAPHORE_UP_WITHOUT_YIELD */, 1, 0, 0).raw;

  __asm__ __volatile__
    ("@ l4_usem_up (semaphore block) \n\t"
     PIC_SAVE_ASM
     "mov     lr, pc       \n\t"
     "mov     pc, %[sys]   \n\t"
     PIC_RESTORE_ASM
     :
     "=r"(_lock),
     "=r"(_timeout),
     "=r"(_flags),
     "=r"(_tag)
     :
     "0"(_lock),
     "1"(_timeout),
     "2"(_flags),
     "3"(_tag),
     [sys] "i" (L4_SYSCALL_INVOKE)
     :
     "cc", "memory", "lr");

  l4_msgtag_t t; t.raw = _tag;
  return t;
}

