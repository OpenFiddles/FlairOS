#include "../include/drv_net.h"

static struct net_device *devices[NET_MAX_DEVICES];
static unsigned int device_count;

static int net_valid(const struct net_device *dev) {
    return dev && dev->name && dev->ops && dev->ops->send &&
           dev->ops->receive && dev->mtu >= 576;
}

int net_register(struct net_device *dev) {
    if (!net_valid(dev) || device_count >= NET_MAX_DEVICES)
        return -1;
    for (unsigned int i = 0; i < device_count; ++i)
        if (devices[i] == dev)
            return -1;
    dev->ready = 1;
    devices[device_count++] = dev;
    return 0;
}

int net_device_count(void) { return (int)device_count; }

struct net_device *net_device_at(unsigned int index) {
    return index < device_count ? devices[index] : 0;
}

struct net_device *net_default(void) {
    return device_count ? devices[0] : 0;
}

int net_link_up(struct net_device *dev) {
    if (!dev || !dev->ready)
        return 0;
    return dev->ops->link_up ? dev->ops->link_up(dev) : 1;
}

int net_send(struct net_device *dev, const void *frame, u16 length) {
    if (!dev || !dev->ready || !frame || length < 14 ||
        length > (u16)(dev->mtu + 14u))
        return -1;
    return dev->ops->send(dev, frame, length);
}

int net_receive(struct net_device *dev, void *frame, u16 capacity) {
    if (!dev || !dev->ready || !frame || !capacity)
        return -1;
    return dev->ops->receive(dev, frame, capacity);
}
