/*
 * (c) 2011 Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */

#include "vbus_factory.h"
#include "resource.h"
#include "hw_msi.h"
#include "vmsi.h"
#include "debug.h"

namespace Vi {
  Msi_resource::Msi_resource(Hw::Msi_resource *hr)
  : Adr_resource(Resource::Irq_res | Resource::Irq_edge | Resource::F_disabled, 0, 0), _hw_msi(hr)
  {
    d_printf(DBG_ALL, "Create virtual MSI wrapper for MSI %ld\n", _hw_msi->start());
  }
}

namespace {
  static Vi::Resource_factory_t<Vi::Msi_resource, Hw::Msi_resource> __vmsifactory;
}

