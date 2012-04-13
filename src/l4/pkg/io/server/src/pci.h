/*
 * (c) 2010 Adam Lackorzynski <adam@os.inf.tu-dresden.de>,
 *          Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include "hw_device.h"


class Pci_bridge;
namespace Hw { namespace Pci {

  enum Cfg_width
  {
    Cfg_long  = 2,
    Cfg_short = 1,
    Cfg_byte  = 0,
  };

  inline Cfg_width cfg_w_to_o(int width)
  {
    switch (width)
      {
      default:
      case 32: return Cfg_long;
      case 16: return Cfg_short;
      case 8:  return Cfg_byte;
      }
  }

  template< Cfg_width w > struct Cfg_type;
  template<> struct Cfg_type<Cfg_byte>  { typedef l4_uint8_t Type; };
  template<> struct Cfg_type<Cfg_short> { typedef l4_uint16_t Type; };
  template<> struct Cfg_type<Cfg_long>  { typedef l4_uint32_t Type; };

  class If : public virtual Dev_if, public Dev_feature
  {
  public:

    virtual Adr_resource *bar(int) const = 0;
    virtual Adr_resource *rom() const = 0;
    virtual bool is_64bit_high_bar(int) const = 0;
    virtual bool supports_msi() const = 0;
    virtual bool is_bridge() const = 0;

    virtual int cfg_read(l4_uint32_t reg, l4_uint32_t *value, Cfg_width) = 0;
    virtual int cfg_write(l4_uint32_t reg, l4_uint32_t value, Cfg_width) = 0;
    virtual l4_uint32_t const *cfg_word(unsigned w) const = 0;
    virtual unsigned bus_nr() const = 0;
    virtual Pci_bridge *bus() const = 0;

    virtual ~If() {}
  };
}}




class Pci_dev : public virtual Hw::Pci::If, public Hw::Discover_res_if
{
protected:
  Hw::Device *_host;
  Pci_bridge *_bus;

public:
  l4_uint32_t vendor_device;
  l4_uint32_t cls_rev;
  l4_uint32_t subsys_ids;
  l4_uint8_t  hdr_type;
  l4_uint8_t  irq_pin;
  l4_uint16_t flags;

private:
  Adr_resource *_bars[6];
  Adr_resource *_rom;


public:

  typedef Hw::Pci::Cfg_width Cfg_width;

  enum Flags
  {
    F_msi = 1
  };

  enum Cfg_regs
  {
    /* Header type 0 config, normal PCI devices */
    C_vendor         = 0x00,
    C_device         = 0x02,
    C_command        = 0x04,
    C_status         = 0x06,
    C_class_rev      = 0x08,
    C_cacheline_size = 0x0c,
    C_latency_timer  = 0x0d,
    C_header_type    = 0x0e,
    C_BIST           = 0x0f,
    C_bar_0          = 0x10,
    C_cardbus_cis    = 0x28,
    C_subsys_vendor  = 0x2c,
    C_subsys         = 0x2e,
    C_rom_address    = 0x30,
    C_capability_ptr = 0x34,
    C_irq_line       = 0x3c,
    C_irq_pin        = 0x3d,
    C_min_gnt        = 0x3e,
    C_max_lat        = 0x3f,

    /* Header type 1, PCI-PCI bridges */
    C_primary           = 0x18,
    C_secondary         = 0x19,
    C_subordinate       = 0x1a,
    C_secondary_latency = 0x1b,
    C_io_base           = 0x1c,
    C_io_limit          = 0x1d,
    C_secondary_status  = 0x1e,
    C_mem_base          = 0x20,
    C_mem_limit         = 0x22,
    C_pref_mem_base     = 0x24,
    C_pref_mem_limit    = 0x26,
    C_pref_mem_base_hi  = 0x28,
    C_pref_mem_limit_hi = 0x2c,
    C_io_base_hi        = 0x30,
    C_io_limit_hi       = 0x32,
    C_rom_address_1     = 0x38,
    C_bridge_control    = 0x3e,

    /* header type 2, cardbus bridge */
    C_cb_capability_ptr   = 0x14,
    C_cb_secondary_status = 0x16,
    C_cb_primary          = 0x18,
    C_cb_cardbus          = 0x19,
    C_cb_subordinate      = 0x1a,
    C_cb_latency_timer    = 0x1b,
    C_cb_mem_base_0       = 0x1c,
    C_cb_mem_limit_0      = 0x20,
    C_cb_mem_base_1       = 0x24,
    C_cb_mem_limit_1      = 0x28,
    C_cb_io_base_0        = 0x2c,
    C_cb_io_base_0_hi     = 0x2e,
    C_cb_io_limit_0       = 0x30,
    C_cb_io_limit_0_hi    = 0x32,
    C_cb_io_base_1        = 0x34,
    C_cb_io_base_1_hi     = 0x36,
    C_cb_io_limit_1       = 0x38,
    C_cb_io_limi_1_hi     = 0x3a,
    C_cb_bridge_control   = 0x3e,
    C_cb_subsystem_vendor = 0x40,
    C_cb_subsystem        = 0x42,
    C_cb_legacy_mode_base = 0x44,

  };

  enum Cfg_status
  {
    CS_cap_list = 0x10,
    CS_66_mhz   = 0x20,
    CS_sig_target_abort = 0x0800,
    CS_rec_target_abort = 0x1000,
    CS_rec_master_abort = 0x2000,
    CS_sig_system_error = 0x4000,
    CS_detected_parity_error = 0x8000,
  };

  enum Cfg_command
  {
    CC_io          = 0x0001,
    CC_mem         = 0x0002,
    CC_bus_master  = 0x0004,
    CC_serr        = 0x0100,
    CC_int_disable = 0x0400,
  };



  Adr_resource *bar(int b) const
  {
    if (is_64bit_high_bar(b))
      return _bars[b-1];
    else
      return _bars[b];
  }

  Adr_resource *rom() const
  { return _rom; }

  bool is_64bit_high_bar(int b) const
  {
    return l4_addr_t(_bars[b]) == 1;
  }

  explicit Pci_dev(Hw::Device *host, Pci_bridge *bus)
  : _host(host), _bus(bus), vendor_device(0), cls_rev(0), flags(0), _rom(0)
  {
    for (unsigned i = 0; i < sizeof(_bars)/sizeof(_bars[0]); ++i)
      _bars[i] = 0;
  }

  Hw::Device *host() const { return _host; }

  bool supports_msi() const { return flags & F_msi; }

  bool is_bridge() const
  { return (cls_rev >> 16) == 0x0604 && (hdr_type & 0x7f) == 1; }

  int cfg_read(l4_uint32_t reg, l4_uint32_t *value, Cfg_width);
  int cfg_write(l4_uint32_t reg, l4_uint32_t value, Cfg_width);
  l4_uint32_t const *cfg_word(unsigned w) const;
  unsigned bus_nr() const;
  Pci_bridge *bus() const { return _bus; }

  void setup_resources(Hw::Device *host);
  void discover_resources(Hw::Device *host);

  bool match_cid(cxx::String const &cid) const;
  void dump(int indent) const;

  int vendor() const { return vendor_device & 0xffff; }
  int device() const { return (vendor_device >> 16) & 0xffff; }

  unsigned function_nr() const { return _host->adr() & 0x07; }
  unsigned device_nr() const { return (_host->adr() >> 16) & 0x1f; }

  unsigned disable_decoders();
  void restore_decoders(unsigned cmd);

private:
  int discover_bar(int bar);
  void discover_expansion_rom();
  void discover_legacy_ide_resources();
  void discover_pci_caps();

  void quirk_8086_8108();
};

class Pci_root_bridge;

class Pci_irq_router : public Resource
{
public:
  Pci_irq_router() : Resource(Irq_res) {}
  void dump(int) const;
  bool compatible(Resource *consumer, bool = true) const
  {
    // only relative CPU IRQ lines are compatible with IRQ routing
    // global IRQs must be allocated at a higher level
    return consumer->type() == Irq_res && consumer->flags() & F_relative;
  }

};

template< typename RES_SPACE >
class Pci_irq_router_res : public Pci_irq_router
{
protected:
  typedef RES_SPACE Irq_rs;
  mutable Irq_rs _rs;

public:
  RES_SPACE *provided() const { return &_rs; }
};


class Pci_pci_bridge_irq_router_rs : public Resource_space
{
public:
  bool request(Resource *parent, Device *, Resource *child, Device *cdev);
  bool alloc(Resource *, Device *, Resource *, Device *, bool)
  { return false; }
};


class Pci_bridge : public virtual Hw::Discover_bus_if
{
public:
  typedef Hw::Pci::Cfg_width Cfg_width;

  unsigned char num;
  unsigned char subordinate;

  explicit Pci_bridge(unsigned char bus) : num(bus), subordinate(bus) {}

  virtual int cfg_read(unsigned bus, l4_uint32_t devfn,
                       l4_uint32_t reg, l4_uint32_t *value, Cfg_width) = 0;
  virtual int cfg_write(unsigned bus, l4_uint32_t devfn,
                        l4_uint32_t reg, l4_uint32_t value, Cfg_width) = 0;

  void scan_bus();
  void dump(int) const;

  virtual void increase_subordinate(int s) = 0;

  virtual ~Pci_bridge() {}
};

class Pci_pci_bridge_basic : public Pci_bridge, public Pci_dev
{
public:
  typedef Pci_dev::Cfg_width Cfg_width;

  unsigned char pri;

  using Pci_dev::cfg_write;
  using Pci_dev::cfg_read;
  using Pci_bridge::cfg_write;
  using Pci_bridge::cfg_read;

  explicit Pci_pci_bridge_basic(Hw::Device *host, Pci_bridge *bus)
  : Pci_bridge(0), Pci_dev(host, bus), pri(0)
  {}

  void increase_subordinate(int x)
  {
    if (subordinate < x)
      {
	subordinate = x;
	cfg_write(Pci_dev::C_subordinate, x, Hw::Pci::Cfg_byte);
	_bus->increase_subordinate(x);
      }
  }

  int cfg_read(unsigned bus, l4_uint32_t devfn,
               l4_uint32_t reg, l4_uint32_t *value, Cfg_width width)
  { return _bus->cfg_read(bus, devfn, reg, value, width); }

  int cfg_write(unsigned bus, l4_uint32_t devfn,
                l4_uint32_t reg, l4_uint32_t value, Cfg_width width)
  { return _bus->cfg_write(bus, devfn, reg, value, width); }

  void dump(int indent) const
  {
    Pci_dev::dump(indent);
    Pci_bridge::dump(indent);
  }

};


class Pci_pci_bridge : public Pci_pci_bridge_basic
{
public:
  Adr_resource *mmio;
  Adr_resource *pref_mmio;
  Adr_resource *io;

  explicit Pci_pci_bridge(Hw::Device *host, Pci_bridge *bus)
  : Pci_pci_bridge_basic(host, bus), mmio(0), pref_mmio(0), io(0)
  {}

  void setup_resources(Hw::Device *host);
  void discover_resources(Hw::Device *host);
};

class Pci_root_bridge : public Pci_bridge
{
private:
  Hw::Device *_host;

public:
  explicit Pci_root_bridge(unsigned bus_nr, Hw::Device *host)
  : Pci_bridge(bus_nr), _host(host)
  {}


  void set_host(Hw::Device *host) { _host = host; }

  Hw::Device *host() const { return _host; }

  void discover_resources(Hw::Device *host);
  void setup_resources(Hw::Device *host);
  void increase_subordinate(int x)
  {
    if (x > subordinate)
      subordinate = x;
  }
};

struct Pci_port_root_bridge : public Pci_root_bridge
{
  explicit Pci_port_root_bridge(unsigned bus_nr, Hw::Device *host)
  : Pci_root_bridge(bus_nr, host) {}

  int cfg_read(unsigned  bus, l4_uint32_t devfn, l4_uint32_t reg,
               l4_uint32_t *value, Cfg_width);

  int cfg_write(unsigned bus, l4_uint32_t devfn, l4_uint32_t reg,
                l4_uint32_t value, Cfg_width);
};

Pci_root_bridge *pci_root_bridge(int segment);
int pci_register_root_bridge(int segment, Pci_root_bridge *b);


// IMPLEMENTATION ------------------------------------------------------

inline
int
Pci_dev::cfg_read(l4_uint32_t reg, l4_uint32_t *value, Cfg_width width)
{
  return _bus->cfg_read(_bus->num, _host->adr(), reg, value, width);
}

inline
int
Pci_dev::cfg_write(l4_uint32_t reg, l4_uint32_t value, Cfg_width width)
{
  return _bus->cfg_write(_bus->num, _host->adr(), reg, value, width);
}

