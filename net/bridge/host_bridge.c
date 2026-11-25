/* SPDX-License-Identifier: AGPL-3.0-only */
/* QEMU <-> Termux Host Bridge client (Telemetry v2 + HTTP proxy) */
#include "host_bridge.h"
#include "../../include/hal.h"

static struct host_telemetry g_tel = {
    0, -1, 1, 0,
    0, 0, 0,
    0, 0, 0,
    0, 0, 0,
    "", "", "", "", "", "hardware"
};
static char g_rx[256];
static int g_rx_len = 0;
static uint32_t g_poll_cnt = 0;
static int g_inited = 0;

static void b_scpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}

static int parse_int(const char *s, int *out_idx) {
    int i = *out_idx, v = 0, neg = 0;
    while (s[i] == ' ') i++;
    if (s[i] == '-') { neg = 1; i++; }
    while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; }
    *out_idx = i;
    return neg ? -v : v;
}

static void parse_pipe_field(const char *s, int *io_idx, char *out, int cap) {
    int i = *io_idx, o = 0;
    if (s[i] == '|') i++;
    while (s[i] && s[i] != '|' && s[i] != '\n' && s[i] != '\r') {
        if (o + 1 < cap) out[o++] = s[i];
        i++;
    }
    if (cap > 0) out[o] = 0;
    *io_idx = i;
}

static void bridge_send_str(const char *s) {
#ifdef FLAIROS_HOST_TEST
    (void)s;
#else
    while (s && *s) hal_bridge_putc(*s++);
#endif
}

static int bridge_try_getc(void) {
#ifdef FLAIROS_HOST_TEST
    return -1;
#else
    return hal_bridge_getc();
#endif
}

void host_bridge_init(void) {
    if (g_inited) return;
    g_inited = 1;
#ifndef FLAIROS_HOST_TEST
    hal_bridge_init();
#endif
    g_tel.valid = 0;
    g_tel.battery_pct = -1;
    g_tel.ac_power = 1;
    g_tel.battery_temp_c10 = 0;
    g_tel.wifi_up = 0;
    g_tel.wifi_rssi = 0;
    g_tel.wifi_bars = 0;
    g_tel.cell_up = 0;
    g_tel.cell_bars = 0;
    g_tel.cell_roaming = 0;
    g_tel.bt_available = 0;
    g_tel.bt_powered = 0;
    g_tel.bt_connected = 0;
    g_tel.wifi_ssid[0] = 0;
    g_tel.wifi_ip[0] = 0;
    g_tel.cell_carrier[0] = 0;
    g_tel.cell_rat[0] = 0;
    g_tel.bt_name[0] = 0;
    b_scpy(g_tel.source, "hardware", sizeof(g_tel.source));
    bridge_send_str("STAT2\n");
}

static void handle_line(const char *line) {
    if (!line) return;
    if (line[0] == 'S' && line[1] == 'T' && line[2] == 'A' && line[3] == 'T' &&
        line[4] == '2' && line[5] == ' ') {
        int idx = 6;
        int pct      = parse_int(line, &idx);
        int ac       = parse_int(line, &idx);
        int temp_c10 = parse_int(line, &idx);
        int w_up     = parse_int(line, &idx);
        int w_rssi   = parse_int(line, &idx);
        int w_bars   = parse_int(line, &idx);
        int c_up     = parse_int(line, &idx);
        int c_bars   = parse_int(line, &idx);
        int c_roam   = parse_int(line, &idx);
        int bt_avail = parse_int(line, &idx);
        int bt_pwr   = parse_int(line, &idx);
        int bt_conn  = parse_int(line, &idx);
        if (pct < 0 || pct > 100) return;
        g_tel.valid = 1;
        g_tel.battery_pct = pct;
        g_tel.ac_power = ac ? 1 : 0;
        g_tel.battery_temp_c10 = temp_c10;
        g_tel.wifi_up = w_up ? 1 : 0;
        g_tel.wifi_rssi = w_rssi;
        g_tel.wifi_bars = (w_bars >= 0 && w_bars <= 4) ? w_bars : (w_up ? 4 : 0);
        g_tel.cell_up = c_up ? 1 : 0;
        g_tel.cell_bars = (c_bars >= 0 && c_bars <= 4) ? c_bars : (c_up ? 4 : 0);
        g_tel.cell_roaming = c_roam ? 1 : 0;
        g_tel.bt_available = bt_avail ? 1 : 0;
        g_tel.bt_powered = bt_pwr ? 1 : 0;
        g_tel.bt_connected = bt_conn ? 1 : 0;
        if (line[idx] == '|') {
            parse_pipe_field(line, &idx, g_tel.wifi_ssid, sizeof(g_tel.wifi_ssid));
            parse_pipe_field(line, &idx, g_tel.wifi_ip, sizeof(g_tel.wifi_ip));
            parse_pipe_field(line, &idx, g_tel.cell_carrier, sizeof(g_tel.cell_carrier));
            parse_pipe_field(line, &idx, g_tel.cell_rat, sizeof(g_tel.cell_rat));
            parse_pipe_field(line, &idx, g_tel.bt_name, sizeof(g_tel.bt_name));
            parse_pipe_field(line, &idx, g_tel.source, sizeof(g_tel.source));
        }
        if (!g_tel.source[0]) b_scpy(g_tel.source, "termux-bridge", sizeof(g_tel.source));
    } else if (line[0] == 'S' && line[1] == 'T' && line[2] == 'A' && line[3] == 'T' && line[4] == ' ') {
        int idx = 5;
        int pct = parse_int(line, &idx);
        int ac  = parse_int(line, &idx);
        int wf  = parse_int(line, &idx);
        if (pct >= 0 && pct <= 100) {
            g_tel.valid = 1;
            g_tel.battery_pct = pct;
            g_tel.ac_power = ac ? 1 : 0;
            g_tel.wifi_up = wf ? 1 : 0;
            g_tel.wifi_bars = wf ? 4 : 0;
            b_scpy(g_tel.source, "termux-bridge", sizeof(g_tel.source));
        }
    }
}

void host_bridge_inject_line(const char *line) {
    if (!g_inited) host_bridge_init();
    handle_line(line);
}

void host_bridge_poll(void) {
    if (!g_inited) host_bridge_init();
    for (int iter = 0; iter < 64; iter++) {
        int c = bridge_try_getc();
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            if (g_rx_len > 0) {
                g_rx[g_rx_len] = 0;
                handle_line(g_rx);
                g_rx_len = 0;
            }
        } else if (g_rx_len + 1 < (int)sizeof(g_rx)) {
            g_rx[g_rx_len++] = (char)c;
        }
    }
    g_poll_cnt++;
    if ((g_poll_cnt % 120u) == 0u) {
        bridge_send_str("STAT2\n");
    }
}

const struct host_telemetry *host_bridge_telemetry(void) {
    return &g_tel;
}

int host_bridge_http_get(const char *url, char *out_buf, int max_len, int *out_status) {
    if (!url || !out_buf || max_len <= 1) return -1;
    if (out_status) *out_status = 0;
#ifdef FLAIROS_HOST_TEST
    (void)url;
    return -1;
#else
    if (!g_inited) host_bridge_init();
    while (bridge_try_getc() >= 0) {}
    bridge_send_str("GET ");
    bridge_send_str(url);
    bridge_send_str("\n");

    char line[256];
    int lp = 0, got_hdr = 0, status = 0, body_pos = 0;
    for (uint32_t spins = 0; spins < 3000000u; spins++) {
        int c = bridge_try_getc();
        if (c < 0) continue;
        if (c == '\n') {
            line[lp] = 0;
            if (!got_hdr) {
                if (line[0] == 'H' && line[1] == 'T' && line[2] == 'T' && line[3] == 'P') {
                    int idx = 5;
                    status = parse_int(line, &idx);
                    got_hdr = 1;
                }
            } else {
                if (line[0] == 'E' && line[1] == 'N' && line[2] == 'D' && line[3] == '_' &&
                    line[4] == 'H' && line[5] == 'T' && line[6] == 'T' && line[7] == 'P') {
                    out_buf[body_pos] = 0;
                    if (out_status) *out_status = status;
                    return body_pos;
                }
                for (int i = 0; line[i] && body_pos + 2 < max_len; i++) out_buf[body_pos++] = line[i];
                if (body_pos + 2 < max_len) out_buf[body_pos++] = '\n';
            }
            lp = 0;
        } else if (c != '\r' && lp + 1 < (int)sizeof(line)) {
            line[lp++] = (char)c;
        }
    }
    return -1;
#endif
}
