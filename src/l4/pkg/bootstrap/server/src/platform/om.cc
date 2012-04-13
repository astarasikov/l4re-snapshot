/**
 * \file   support_om.cc
 * \brief  Support for the OpenMoko platform
 *
 * \date   2008
 * \author Adam Lackorznynski <adam@os.inf.tu-dresden.de>
 *
 */
/*
 * (c) 2008-2009 Author(s)
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */


#include "support.h"

#include <l4/drivers/uart_s3c2410.h>
#include <l4/drivers/uart_dummy.h>

#define UART_TYPE Uart_s3c2410
//#define UART_TYPE Uart_dummy

namespace {
class Platform_arm_om : public Platform_single_region_ram
{
  bool probe() { return true; }

  void init()
  {
    static L4::UART_TYPE _uart(1,1);
    _uart.startup(0x50000000);
    set_stdio_uart(&_uart);
  }
};
}

REGISTER_PLATFORM(Platform_arm_om);
