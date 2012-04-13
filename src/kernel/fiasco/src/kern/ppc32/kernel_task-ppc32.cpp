IMPLEMENTATION[ppc32]:

#include "config.h"
#include "globals.h"
#include "space.h"

PRIVATE inline NEEDS["globals.h"]
Kernel_task::Kernel_task()
: Task(Space::Default_factory(), Ram_quota::root, Kmem::kdir())
{}
