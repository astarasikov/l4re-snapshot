#ifndef L4_CXX_UART_H__
#define L4_CXX_UART_H__

namespace L4 
{
  class Uart
  {
  protected:
    int _rx_irq;
    int _tx_irq;
    unsigned _mode;
    unsigned _rate;

  public:
    void *operator new (__SIZE_TYPE__, void* a) 
    { return a; }

  public:
    typedef unsigned Transfer_mode;
    typedef unsigned Baud_rate;

    Uart(int rx_irq, int tx_irq)
    : _rx_irq(rx_irq), _tx_irq(tx_irq), _mode(~0UL), _rate(~0UL)
    {}
    
    virtual bool startup(unsigned long base) = 0;

    virtual ~Uart() {}
    virtual void shutdown() = 0;
    virtual bool enable_rx_irq(bool enable = true) = 0;
    virtual bool enable_tx_irq(bool enable = true) = 0;
    virtual bool change_mode(Transfer_mode m, Baud_rate r) = 0;
    virtual int get_char(bool blocking = true) const = 0;
    virtual int char_avail() const = 0;
    virtual int write(char const *s, unsigned long count) const = 0;
    
    int rx_irq() const { return _rx_irq; }
    int tx_irq() const { return _tx_irq; }
    Transfer_mode mode() const { return _mode; }
    Baud_rate rate() const { return _rate; }
  };
};

#endif

