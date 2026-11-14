#ifndef AIROS_DRV_NET_H
#define AIROS_DRV_NET_H

#include "airos_types.h"

#define NET_NAME_MAX 16
#define NET_MAX_DEVICES 4
#define NET_MTU 1500
#define NET_FRAME_MAX 1514

struct net_device;

struct net_device_ops {
    int (*link_up)(struct net_device *dev);
    int (*send)(struct net_device *dev, const void *frame, u16 length);
    int (*receive)(struct net_device *dev, void *frame, u16 capacity);
};

struct net_device {
    const char *name;
    u8 mac[6];
    u16 mtu;
    void *private_data;
    const struct net_device_ops *ops;
    int ready;
};

/* Hardware drivers register devices; the protocol stack uses only this API. */
int net_register(struct net_device *dev);
int net_device_count(void);
struct net_device *net_default(void);
struct net_device *net_device_at(unsigned int index);
int net_link_up(struct net_device *dev);
int net_send(struct net_device *dev, const void *frame, u16 length);
int net_receive(struct net_device *dev, void *frame, u16 capacity);

#endif
