IMPLEMENTATION[arm]:

#include "config.h"
#include "globals.h"
#include "kmem_space.h"

PRIVATE inline NEEDS["globals.h"]
Kernel_task::Kernel_task()
: Task(Space::Default_factory(), Ram_quota::root, Kmem_space::kdir())
{}

