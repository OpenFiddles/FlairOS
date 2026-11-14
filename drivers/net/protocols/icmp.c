#include "../../../include/drv_net.h"

#define ETH_IP 0x0800u
#define ETH_ARP 0x0806u
#define IP_UDP 17u
#define IP_ICMP 1u
#define DHCP_XID 0x464C5231u
#define NET_WAIT_TICKS 150u

extern void wait_ticks(u32 count);

struct net_lease {
    u8 ip[4], mask[4], gateway[4], server[4];
    int valid;
};
static struct net_lease lease;

static u16 get16(const u8 *p) { return (u16)(((u16)p[0] << 8) | p[1]); }
static void put16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static u32 get32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static void put32(u8 *p, u32 v) {
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}
static u16 checksum(const u8 *p, u32 length) {
    u32 sum = 0;
    while (length > 1) { sum += get16(p); p += 2; length -= 2; }
    if (length) sum += (u16)((u16)p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)~sum;
}
static int same_ip(const u8 *a, const u8 *b) {
    for (int i = 0; i < 4; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static void copy_bytes(u8 *d, const u8 *s, u32 n) {
    for (u32 i = 0; i < n; ++i) d[i] = s[i];
}
static void zero_bytes(u8 *d, u32 n) { for (u32 i = 0; i < n; ++i) d[i] = 0; }

static int send_udp_broadcast(struct net_device *dev, const u8 *mac,
                              const u8 *payload, u16 payload_len,
                              u16 source_port, u16 dest_port) {
    u8 frame[NET_FRAME_MAX];
    u16 ip_len = (u16)(20u + 8u + payload_len);
    if (14u + ip_len > sizeof(frame)) return -1;
    copy_bytes(frame, (const u8 *)"\xFF\xFF\xFF\xFF\xFF\xFF", 6);
    copy_bytes(frame + 6, mac, 6);
    put16(frame + 12, ETH_IP);
    frame[14] = 0x45; frame[15] = 0;
    put16(frame + 16, ip_len); put16(frame + 18, 0x4F31);
    put16(frame + 20, 0); frame[22] = 64; frame[23] = IP_UDP;
    put16(frame + 24, 0); zero_bytes(frame + 26, 4);
    frame[30] = 255; frame[31] = 255; frame[32] = 255; frame[33] = 255;
    put16(frame + 24, checksum(frame + 14, 20));
    put16(frame + 34, source_port); put16(frame + 36, dest_port);
    put16(frame + 38, (u16)(8u + payload_len)); put16(frame + 40, 0);
    copy_bytes(frame + 42, payload, payload_len);
    return net_send(dev, frame, (u16)(14u + ip_len));
}
static int dhcp_send(struct net_device *dev, u8 message_type,
                     const u8 *mac, const u8 *requested, const u8 *server) {
    u8 packet[300];
    u16 n = 240;
    zero_bytes(packet, sizeof(packet));
    packet[0] = 1; packet[1] = 1; packet[2] = 6;
    put32(packet + 4, DHCP_XID);
    packet[10] = 0x80; /* broadcast reply */
    copy_bytes(packet + 28, mac, 6);
    packet[236] = 99; packet[237] = 130; packet[238] = 83; packet[239] = 99;
    packet[n++] = 53; packet[n++] = 1; packet[n++] = message_type;
    if (message_type == 3 && requested && server) {
        packet[n++] = 50; packet[n++] = 4; copy_bytes(packet + n, requested, 4); n += 4;
        packet[n++] = 54; packet[n++] = 4; copy_bytes(packet + n, server, 4); n += 4;
    }
    packet[n++] = 55; packet[n++] = 4;
    packet[n++] = 1; packet[n++] = 3; packet[n++] = 6; packet[n++] = 51;
    packet[n++] = 61; packet[n++] = 7; packet[n++] = 1;
    copy_bytes(packet + n, mac, 6); n += 6;
    packet[n++] = 255;
    return send_udp_broadcast(dev, mac, packet, n, 68, 67);
}
static int parse_dhcp(const u8 *frame, int length, u8 wanted,
                      const u8 *mac, struct net_lease *out) {
    u16 ip_len, udp_len;
    const u8 *ip, *udp, *p, *options;
    u32 xid;
    int option_type = 0;
    if (length < 14 + 20 + 8 + 240 || get16(frame + 12) != ETH_IP) return 0;
    ip = frame + 14;
    if ((ip[0] >> 4) != 4 || ip[9] != IP_UDP) return 0;
    ip_len = get16(ip + 2);
    if (ip_len < 28 || ip_len > (u16)(length - 14)) return 0;
    udp = ip + (ip[0] & 0x0Fu) * 4u;
    udp_len = get16(udp + 4);
    if (get16(udp) != 67 || get16(udp + 2) != 68 || udp_len < 8 + 240) return 0;
    if ((u32)(udp - ip) + udp_len > ip_len) return 0;
    p = udp + 8;
    if (p[0] != 2 || p[1] != 1 || p[2] != 6) return 0;
    xid = get32(p + 4);
    if (xid != DHCP_XID) return 0;
    for (int i = 0; i < 6; ++i) if (p[28 + i] != mac[i]) return 0;
    if (p[236] != 99 || p[237] != 130 || p[238] != 83 || p[239] != 99) return 0;
    copy_bytes(out->ip, p + 16, 4);
    zero_bytes(out->mask, 4); zero_bytes(out->gateway, 4); zero_bytes(out->server, 4);
    options = p + 240;
    {
        u32 off = 240;
        u32 end = udp_len - 8;
        while (off < end) {
            u8 code = p[off++];
            u8 len;
            if (code == 255) break;
            if (code == 0) continue;
            if (off >= end) break;
            len = p[off++];
            if (off + len > end) break;
            if (code == 53 && len == 1) option_type = p[off];
            else if (code == 1 && len == 4) copy_bytes(out->mask, p + off, 4);
            else if (code == 3 && len >= 4) copy_bytes(out->gateway, p + off, 4);
            else if (code == 54 && len == 4) copy_bytes(out->server, p + off, 4);
            off += len;
        }
    }
    (void)options;
    return option_type == wanted;
}
static int wait_dhcp(struct net_device *dev, u8 wanted, struct net_lease *out) {
    u8 frame[NET_FRAME_MAX];
    for (u32 t = 0; t < NET_WAIT_TICKS; ++t) {
        int n = net_receive(dev, frame, sizeof(frame));
        if (n > 0 && parse_dhcp(frame, n, wanted, dev->mac, out)) return 0;
        wait_ticks(1);
    }
    return -1;
}
static int dhcp_configure(struct net_device *dev) {
    struct net_lease offer;
    if (dhcp_send(dev, 1, dev->mac, 0, 0) != 0) return -1;
    if (wait_dhcp(dev, 2, &offer) != 0) return -1;
    if (dhcp_send(dev, 3, dev->mac, offer.ip, offer.server) != 0) return -1;
    if (wait_dhcp(dev, 5, &lease) != 0) return -1;
    if (same_ip(lease.ip, (const u8 *)"\0\0\0\0")) return -1;
    lease.valid = 1;
    return 0;
}
static int arp_resolve(struct net_device *dev, const u8 *target, u8 *mac_out) {
    u8 frame[NET_FRAME_MAX];
    u8 arp[28];
    copy_bytes(frame, (const u8 *)"\xFF\xFF\xFF\xFF\xFF\xFF", 6);
    copy_bytes(frame + 6, dev->mac, 6); put16(frame + 12, ETH_ARP);
    put16(arp, 1); put16(arp + 2, ETH_IP); arp[4] = 6; arp[5] = 4; put16(arp + 6, 1);
    copy_bytes(arp + 8, dev->mac, 6); copy_bytes(arp + 14, lease.ip, 4);
    zero_bytes(arp + 18, 6); copy_bytes(arp + 24, target, 4);
    copy_bytes(frame + 14, arp, sizeof(arp));
    if (net_send(dev, frame, 42) != 0) return -1;
    for (u32 t = 0; t < NET_WAIT_TICKS; ++t) {
        int n = net_receive(dev, frame, sizeof(frame));
        if (n >= 42 && get16(frame + 12) == ETH_ARP &&
            get16(frame + 20) == 2 &&
            same_ip(frame + 28, target) && same_ip(frame + 38, lease.ip)) {
            copy_bytes(mac_out, frame + 22, 6);
            return 0;
        }
        wait_ticks(1);
    }
    return -1;
}
static int send_echo(struct net_device *dev, const u8 *target, const u8 *target_mac) {
    u8 frame[14 + 20 + 16];
    u8 *ip = frame + 14, *icmp = ip + 20;
    copy_bytes(frame, target_mac, 6); copy_bytes(frame + 6, dev->mac, 6);
    put16(frame + 12, ETH_IP);
    ip[0] = 0x45; ip[1] = 0; put16(ip + 2, 36); put16(ip + 4, 0x4631);
    put16(ip + 6, 0); ip[8] = 64; ip[9] = IP_ICMP; put16(ip + 10, 0);
    copy_bytes(ip + 12, lease.ip, 4); copy_bytes(ip + 16, target, 4);
    put16(ip + 10, checksum(ip, 20));
    icmp[0] = 8; icmp[1] = 0; put16(icmp + 2, 0);
    put16(icmp + 4, 0x464C); put16(icmp + 6, 1);
    copy_bytes(icmp + 8, (const u8 *)"FlareOS!!", 8);
    put16(icmp + 2, checksum(icmp, 16));
    return net_send(dev, frame, sizeof(frame));
}
static int wait_echo(struct net_device *dev, const u8 *target) {
    u8 frame[NET_FRAME_MAX];
    for (u32 t = 0; t < NET_WAIT_TICKS; ++t) {
        int n = net_receive(dev, frame, sizeof(frame));
        if (n >= 14 + 20 + 16 && get16(frame + 12) == ETH_IP) {
            const u8 *ip = frame + 14;
            u32 ihl = (ip[0] & 0x0Fu) * 4u;
            if ((ip[0] >> 4) == 4 && ip[9] == IP_ICMP && ihl >= 20 &&
                n >= (int)(14u + ihl + 16u) && same_ip(ip + 12, target) &&
                same_ip(ip + 16, lease.ip)) {
                const u8 *icmp = ip + ihl;
                if (icmp[0] == 0 && icmp[1] == 0 &&
                    get16(icmp + 4) == 0x464C && get16(icmp + 6) == 1)
                    return 0;
            }
        }
        wait_ticks(1);
    }
    return -1;
}

int net_ping_ipv4(const u8 target[4]) {
    struct net_device *dev = net_default();
    u8 next_hop[4], target_mac[6];
    if (!dev) return -1;
    if (!lease.valid && dhcp_configure(dev) != 0) return -2;
    for (int i = 0; i < 4; ++i)
        next_hop[i] = (u8)((target[i] & lease.mask[i]) |
                           (lease.gateway[i] & (u8)~lease.mask[i]));
    if (arp_resolve(dev, next_hop, target_mac) != 0) return -3;
    if (send_echo(dev, target, target_mac) != 0) return -4;
    return wait_echo(dev, target) == 0 ? 0 : -4;
}
