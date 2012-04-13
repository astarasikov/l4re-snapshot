/*
 * (c) 2011 Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */

#pragma once

#include "resource.h"
#include "irqs.h"

namespace Hw {
  class Msi_resource : public Adr_resource, public Kernel_irq_pin
  {
  public:
    Msi_resource(unsigned msi)
    : Adr_resource(Resource::Irq_res | Resource::Irq_edge, msi, msi),
      Kernel_irq_pin(msi |  L4::Icu::F_msi)
    {}
  };
}
