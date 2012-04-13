#include "uart_pl011.h"

namespace L4
{
  enum {
    UART011_RXIM = 1 << 4,
    UART011_TXIM = 1 << 5,
    UART011_RTIM = 1 << 6,
    UART011_FEIM = 1 << 7,
    UART011_PEIM = 1 << 8,
    UART011_BEIM = 1 << 9,
    UART011_OEIM = 1 << 10,

    UART011_RXIS = 1 << 4,
    UART011_RTIS = 1 << 6,

    UART011_RXIC = 1 << 4,
    UART011_RTIC = 1 << 6,

    UART01x_CR_UARTEN = 1, // UART enable
    UART011_CR_LBE    = 0x080, // loopback enable
    UART011_CR_TXE    = 0x100, // transmit enable
    UART011_CR_RXE    = 0x200, // receive enable

    UART01x_FR_BUSY   = 0x008,
    UART01x_FR_RXFE   = 0x010,
    UART01x_FR_TXFF   = 0x020,

    UART01x_LCRH_PEN    = 0x02, // parity enable
    UART01x_LCRH_FEN    = 0x10, // FIFO enable
    UART01x_LCRH_WLEN_8 = 0x60,

    UART01x_DR   = 0x00,
    UART011_ECR  = 0x04,
    UART01x_FR   = 0x18,
    UART011_IBRD = 0x24,
    UART011_FBRD = 0x28,
    UART011_LCRH = 0x2c,
    UART011_CR   = 0x30,
    UART011_IMSC = 0x38,
    UART011_MIS  = 0x40,
    UART011_ICR  = 0x44,
  };


  unsigned long Uart_pl011::rd(unsigned long reg) const
  {
    volatile unsigned long *r = (unsigned long*)(_base + reg);
    return *r;
  }

  void Uart_pl011::wr(unsigned long reg, unsigned long val) const
  {
    volatile unsigned long *r = (unsigned long*)(_base + reg);
    *r = val;
  }

  bool Uart_pl011::startup(unsigned long base)
  {
    _base = base;
    wr(UART011_CR, UART01x_CR_UARTEN | UART011_CR_TXE | UART011_CR_RXE);
//    wr(UART011_FBRD, 2);
//    wr(UART011_IBRD, 13);
//    wr(UART011_LCRH, 0x60);
    wr(UART011_IMSC, 0);
    while (rd(UART01x_FR) & UART01x_FR_BUSY);
    return true;
  }
  
  void Uart_pl011::shutdown()
  {
    wr(UART011_IMSC,0);
    wr(UART011_ICR, 0xffff);
    wr(UART011_CR, 0);
  }

  bool Uart_pl011::enable_rx_irq(bool enable) 
  {
    unsigned long mask = UART011_RXIM | UART011_RTIM;

    wr(UART011_ICR, 0xffff);
    wr(UART011_ECR, 0xff);
    if (enable)
      wr(UART011_IMSC, rd(UART011_IMSC) | mask);
    else
      wr(UART011_IMSC, rd(UART011_IMSC) & ~mask);
    return true; 
  }
  bool Uart_pl011::enable_tx_irq(bool /*enable*/) { return false; }
  bool Uart_pl011::change_mode(Transfer_mode, Baud_rate r)
  {
    if (r != 115200)
      return false;

    unsigned long old_cr = rd(UART011_CR);
    wr(UART011_CR, 0);

    wr(UART011_FBRD, 2);
    wr(UART011_IBRD, 13);
    wr(UART011_LCRH, UART01x_LCRH_WLEN_8 | UART01x_LCRH_FEN);

    wr(UART011_CR, old_cr);

    return true;
  }

  int Uart_pl011::get_char(bool blocking) const
  { 
    while (!char_avail()) 
      if (!blocking) return -1;

    //wr(UART011_ICR, UART011_RXIC | UART011_RTIC);

    int c = rd(UART01x_DR);
    wr(UART011_ECR, 0xff);
    return c;
  }

  int Uart_pl011::char_avail() const 
  { 
    return !(rd(UART01x_FR) & UART01x_FR_RXFE); 
  }

  void Uart_pl011::out_char(char c) const
  {
    while (rd(UART01x_FR) & UART01x_FR_TXFF)
      ;
    wr(UART01x_DR,c);
  }

  int Uart_pl011::write(char const *s, unsigned long count) const
  {
    unsigned long c = count;
    while (c)
      {
	if (*s == 10)
	  out_char(13);
	out_char(*s++);
	--c;
      }
    while (rd(UART01x_FR) & UART01x_FR_BUSY)
      ;

    return count;
  }

};

