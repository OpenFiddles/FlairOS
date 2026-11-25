#include <stdio.h>
#include "status.h"
#include "drv_net.h"

volatile unsigned int ticks;
static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

/* Stub network core */
static struct net_device devs[3];
static int dev_count, dev_up[3], dev_signal_value = 70;
int net_device_count(void) { return dev_count; }
struct net_device *net_device_at(unsigned int i) { return i < (unsigned int)dev_count ? &devs[i] : 0; }
int net_link_up(struct net_device *d) { return dev_up[d - devs]; }
static int fake_signal(struct net_device *d) { (void)d; return dev_signal_value; }
static const struct net_device_ops wifi_ops = { .signal = fake_signal };
static const struct net_device_ops wired_ops = { 0 };

static struct power_info fake_battery;
static int fake_present;
static int fake_read(struct power_info *o) { if (!fake_present) return 0; *o = fake_battery; return 1; }
static const struct power_provider fake_provider = { "fake", fake_read };

int main(void) {
    struct power_info p;
    struct link_info l;

    /* line protocol */
    CHECK(power_parse_line("BAT 87 1", &p) == 0 && p.present && p.percent == 87 && p.charging == 1);
    CHECK(power_parse_line("BAT 5", &p) == 0 && p.percent == 5 && p.charging == -1);
    CHECK(power_parse_line("BAT 100 0", &p) == 0 && p.percent == 100 && p.charging == 0);
    CHECK(power_parse_line("NOBAT", &p) == 0 && !p.present && p.percent == -1);
    CHECK(power_parse_line("BAT 101", &p) != 0);
    CHECK(power_parse_line("BAT x", &p) != 0);
    CHECK(power_parse_line("BAT 50 7", &p) != 0);
    CHECK(power_parse_line("HELLO", &p) != 0);
    CHECK(power_parse_line(0, &p) != 0);

    /* no provider: honest "not present" */
    CHECK(power_read(&p) == 0 && !p.present && p.percent == -1);
    CHECK(power_register(&fake_provider) == 0);
    CHECK(power_register(&fake_provider) == 0);   /* duplicate is harmless */
    CHECK(power_read(&p) == 0);
    fake_present = 1; fake_battery.present = 1; fake_battery.percent = 42; fake_battery.charging = 0;
    CHECK(power_read(&p) == 1 && p.percent == 42 && p.charging == 0);
    CHECK(power_source_name()[0] == 'f');
    fake_present = 0;
    CHECK(power_read(&p) == 0);

    /* network: none, wired down/up, wireless */
    CHECK(link_read(&l) == 0 && !l.present && !l.up);
    dev_count = 1; devs[0].ops = &wired_ops; dev_up[0] = 0;
    CHECK(link_read(&l) == 1 && l.present && !l.up && !l.wireless);
    dev_up[0] = 1;
    CHECK(link_read(&l) == 1 && l.up && !l.wireless && l.signal == -1);
    devs[0].ops = &wifi_ops;
    CHECK(link_read(&l) == 1 && l.up && l.wireless && l.signal == 70);
    dev_up[0] = 0;
    CHECK(link_read(&l) == 1 && !l.up && l.wireless);
    dev_count = 2; devs[0].ops = &wifi_ops; devs[1].ops = &wired_ops; dev_up[0] = 0; dev_up[1] = 1;
    CHECK(link_read(&l) == 1 && l.up && !l.wireless);   /* the live link is wired */

    if (failures) { printf("status host test: %d failure(s)\n", failures); return 1; }
    printf("status host test: all checks passed\n");
    return 0;
}
