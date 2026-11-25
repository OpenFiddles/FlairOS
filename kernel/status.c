#include "../include/status.h"
#include "../include/drv_net.h"
#ifndef FLAIROS_HOST_TEST
#include "../include/hw.h"
#endif

extern volatile unsigned int ticks;

static const struct power_provider *providers[POWER_MAX_PROVIDERS];
static unsigned int provider_count;
static const char *source_name = "none";
#ifndef FLAIROS_HOST_TEST
static uint8_t bridge_registered;
#endif

static void power_none(struct power_info *o) {
    o->present = 0;
    o->percent = -1;
    o->charging = -1;
}

static int parse_uint(const char **p, int *value) {
    int n = 0, any = 0;
    while (**p == ' ') ++*p;
    while (**p >= '0' && **p <= '9') {
        n = n * 10 + (**p - '0');
        if (n > 1000) return 0;
        ++*p;
        any = 1;
    }
    if (any) *value = n;
    return any;
}

int power_parse_line(const char *line, struct power_info *out) {
    const char *p;
    int pct = 0, chg = -1, tmp = 0;
    if (!line || !out) return -1;
    if (line[0] == 'N' && line[1] == 'O' && line[2] == 'B' && line[3] == 'A' && line[4] == 'T') {
        power_none(out);
        return 0;
    }
    if (!(line[0] == 'B' && line[1] == 'A' && line[2] == 'T' && line[3] == ' ')) return -1;
    p = line + 3;
    if (!parse_uint(&p, &pct) || pct > 100) return -1;
    if (parse_uint(&p, &tmp)) {
        if (tmp > 1) return -1;
        chg = tmp;
    }
    out->present = 1;
    out->percent = (int8_t)pct;
    out->charging = (int8_t)chg;
    return 0;
}

#ifndef FLAIROS_HOST_TEST
/* Serial bridge: a host-side script writes battery lines to the guest's second
   serial port (COM2). Works for any emulator or board that exposes one; if the
   port is absent the provider simply reports no battery. */
#define BR 0x2F8u
static int8_t br_state;            /* 0 untested, 1 live, -1 absent */
static struct power_info br_last;
static uint32_t br_stamp;
static uint8_t br_seen, br_len;
static char br_line[24];

static void bridge_probe(void) {
    hw_outb(BR + 7u, 0xA5u);
    if (hw_inb(BR + 7u) != 0xA5u || hw_inb(BR + 5u) == 0xFFu) { br_state = -1; return; }
    hw_outb(BR + 1u, 0u);
    hw_outb(BR + 3u, 0x80u);
    hw_outb(BR + 0u, 1u);          /* 115200 baud */
    hw_outb(BR + 1u, 0u);
    hw_outb(BR + 3u, 3u);
    hw_outb(BR + 2u, 0xC7u);
    hw_outb(BR + 4u, 0x0Bu);
    power_none(&br_last);
    br_state = 1;
}

static int bridge_read(struct power_info *out) {
    unsigned int n = 0;
    if (br_state == 0) bridge_probe();
    if (br_state < 0) return 0;
    while ((hw_inb(BR + 5u) & 1u) && n++ < 64u) {
        char c = (char)hw_inb(BR);
        if (c == '\n' || c == '\r') {
            if (br_len) {
                struct power_info tmp;
                br_line[br_len] = 0;
                if (power_parse_line(br_line, &tmp) == 0) {
                    br_last = tmp;
                    br_stamp = ticks;
                    br_seen = 1;
                }
                br_len = 0;
            }
        } else if (br_len < sizeof(br_line) - 1u) {
            br_line[br_len++] = c;
        }
    }
    /* A bridge that stops talking for 30 s is treated as gone. */
    if (br_seen && (ticks - br_stamp) > 3000u) { power_none(&br_last); br_seen = 0; }
    if (!br_last.present) return 0;
    *out = br_last;
    return 1;
}

static const struct power_provider serial_bridge = { "serial-bridge", bridge_read };
#endif

int power_register(const struct power_provider *p) {
    unsigned int i;
    if (!p || !p->read) return -1;
    for (i = 0; i < provider_count; ++i)
        if (providers[i] == p) return 0;
    if (provider_count >= POWER_MAX_PROVIDERS) return -1;
    providers[provider_count++] = p;
    return 0;
}

int power_read(struct power_info *out) {
    unsigned int i;
    struct power_info tmp;
    if (!out) return 0;
#ifndef FLAIROS_HOST_TEST
    if (!bridge_registered) { bridge_registered = 1; (void)power_register(&serial_bridge); }
#endif
    for (i = 0; i < provider_count; ++i) {
        power_none(&tmp);
        if (providers[i]->read(&tmp) == 1 && tmp.present) {
            *out = tmp;
            source_name = providers[i]->name ? providers[i]->name : "unnamed";
            return 1;
        }
    }
    power_none(out);
    source_name = "none";
    return 0;
}

const char *power_source_name(void) { return source_name; }

int link_read(struct link_info *out) {
    int i, n, up_wireless = 0, any_wireless = 0;
    if (!out) return 0;
    out->present = 0; out->up = 0; out->wireless = 0; out->signal = -1;
    n = net_device_count();
    for (i = 0; i < n && i < NET_MAX_DEVICES; ++i) {
        struct net_device *d = net_device_at((unsigned int)i);
        int up, has_signal;
        if (!d) continue;
        out->present = 1;
        up = net_link_up(d) ? 1 : 0;
        has_signal = d->ops && d->ops->signal;
        if (has_signal) any_wireless = 1;
        if (up) {
            out->up = 1;
            if (has_signal) {
                int s = d->ops->signal(d);
                up_wireless = 1;
                if (s >= 0 && s <= 100 && s > out->signal) out->signal = (int8_t)s;
            }
        }
    }
    out->wireless = (int8_t)(out->up ? up_wireless : any_wireless);
    return out->present;
}
