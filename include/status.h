#ifndef FLAIROS_STATUS_H
#define FLAIROS_STATUS_H

#include <stdint.h>

/* Generic system status. The desktop asks "is there a battery?" and "is the
   network up?" without knowing which hardware answers. Drivers and bridges
   plug in underneath; when nothing answers, the answer is "not present". */

struct power_info {
    int8_t present;    /* a battery was reported */
    int8_t percent;    /* 0-100, -1 unknown */
    int8_t charging;   /* 1 charging, 0 discharging, -1 unknown */
};

struct power_provider {
    const char *name;
    int (*read)(struct power_info *out);   /* return 1 when a battery is present */
};

#define POWER_MAX_PROVIDERS 4

int power_register(const struct power_provider *provider);
int power_read(struct power_info *out);    /* 1 if any provider reports a battery */
const char *power_source_name(void);
/* Line protocol used by the serial bridge: "BAT <0-100> [0|1]" or "NOBAT". */
int power_parse_line(const char *line, struct power_info *out);

struct link_info {
    int8_t present;    /* at least one network device exists */
    int8_t up;         /* at least one device has link */
    int8_t wireless;   /* the active device reports signal strength */
    int8_t signal;     /* 0-100, -1 unknown or wired */
};

int link_read(struct link_info *out);      /* 1 if any network device exists */

#endif
