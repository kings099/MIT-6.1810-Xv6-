#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static char *tx_bufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static char *rx_bufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_bufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_bufs[i] = kalloc();
    if (!rx_bufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_bufs[i];
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(char *buf, int len)
{
  //
  // Your code here.
  //
  // buf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after send completes.
  //

  acquire(&e1000_lock);
  
  // First ask the E1000 for the TX ring index at which it's expecting the next packet
  uint32 tdt = regs[E1000_TDT];
  
  // Check if the ring is overflowing. If E1000_TXD_STAT_DD is not set in the descriptor
  // indexed by E1000_TDT, the E1000 hasn't finished the corresponding previous transmission request
  if((tx_ring[tdt].status & E1000_TXD_STAT_DD) == 0) {
    // Ring is full, return error
    release(&e1000_lock);
    return -1;
  }
  
  // Free the last buffer that was transmitted from that descriptor (if there was one)
  if(tx_bufs[tdt] != 0) {
    kfree(tx_bufs[tdt]);
  }
  
  // Fill in the descriptor
  tx_ring[tdt].addr = (uint64)buf;
  tx_ring[tdt].length = len;
  tx_ring[tdt].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;  // End of Packet and Report Status
  tx_ring[tdt].status = 0;  // Clear status for new transmission
  
  // Stash away a pointer to the buffer for later freeing
  tx_bufs[tdt] = buf;
  
  // Update the ring position by adding one to E1000_TDT modulo TX_RING_SIZE
  regs[E1000_TDT] = (tdt + 1) % TX_RING_SIZE;
  
  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver a buf for each packet (using net_rx()).
  //

  acquire(&e1000_lock);
  
  // Process all available packets
  while(1) {
    // Ask the E1000 for the ring index at which the next waiting received packet (if any) is located
    // by fetching the E1000_RDT control register and adding one modulo RX_RING_SIZE
    uint32 rdt = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
    
    // Check if a new packet is available by checking for the E1000_RXD_STAT_DD bit in the status portion of the descriptor
    if((rx_ring[rdt].status & E1000_RXD_STAT_DD) == 0) {
      // No new packet available
      break;
    }
    
    // Get the buffer and length
    char *buf = rx_bufs[rdt];
    int len = rx_ring[rdt].length;
    
    // Allocate a new buffer using kalloc() to replace the one just given to net_rx()
    rx_bufs[rdt] = kalloc();
    if(rx_bufs[rdt] == 0) {
      panic("e1000_recv: kalloc failed");
    }
    
    // Update the descriptor with the new buffer
    rx_ring[rdt].addr = (uint64)rx_bufs[rdt];
    rx_ring[rdt].status = 0;  // Clear the descriptor's status bits to zero
    
    // Update the E1000_RDT register to be the index of the last ring descriptor processed
    regs[E1000_RDT] = rdt;
    
    // Release lock before calling net_rx to avoid holding lock too long
    release(&e1000_lock);
    
    // Deliver the packet buffer to the network stack by calling net_rx()
    net_rx(buf, len);
    
    // Reacquire lock for next iteration
    acquire(&e1000_lock);
  }
  
  release(&e1000_lock);
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
