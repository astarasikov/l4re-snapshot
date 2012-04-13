
INTERFACE:

#include "timeout.h"

class Receiver;

class IPC_timeout : public Timeout
{
  friend class Jdb_list_timeouts;
};

IMPLEMENTATION:

#include "context.h"
#include "globals.h"
#include "receiver.h"
#include "thread_state.h"

/**
 * IPC_timeout constructor
 */
PUBLIC inline
IPC_timeout::IPC_timeout()
{}

/**
 * IPC_timeout destructor
 */
PUBLIC virtual inline NEEDS [IPC_timeout::owner, "receiver.h"]
IPC_timeout::~IPC_timeout()
{
  owner()->set_timeout (0);	// reset owner's timeout field
}

PRIVATE inline NEEDS ["globals.h"]
Receiver *
IPC_timeout::owner()
{
  // We could have saved our context in our constructor, but computing
  // it this way is easier and saves space. We can do this as we know
  // that IPC_timeouts are always created on the kernel stack of the
  // owner context.

  return reinterpret_cast<Receiver *>(context_of (this));
}

/**
 * Timeout expiration callback function
 * @return true if reschedule is necessary, false otherwise
 */
PRIVATE
bool
IPC_timeout::expired()
{
  Receiver * const _owner = owner();

  // Set thread ready
  _owner->state_change_dirty(~Thread_ipc_mask, Thread_ready | Thread_timeout);

  // Flag reschedule if owner's priority is higher than the current
  // thread's (own or timeslice-donated) priority.
  return _owner->sched()->deblock(current_cpu(), current()->sched(), false);
}
