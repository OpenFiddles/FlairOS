#ifndef AIROS_DRV_NET_H
#define AIROS_DRV_NET_H

#include "drv.h"

/* Polling raw-Ethernet interface. RTL8139 is the first supported PCI NIC.
   Network protocols (ARP/IP/UDP/TCP) sit above this layer. */
extern struct drv drv_net_device;

const u8 *drv_net_mac(void);
u16 drv_net_io(void);
int drv_net_is_rtl(void);
int drv_net_init(void);
int drv_net_link_up(void);
int drv_net_send(const void *frame, u16 length);
int drv_net_receive(void *frame, u16 capacity);

#endif
