#ifndef FLAIROS_HOST_BRIDGE_H
#define FLAIROS_HOST_BRIDGE_H

#include <stdint.h>
#include "../../include/telemetry_contract.h"

#define host_telemetry flair_telemetry_v2

void host_bridge_init(void);
void host_bridge_poll(void);
void host_bridge_inject_line(const char *line);
const struct host_telemetry *host_bridge_telemetry(void);
int host_bridge_http_get(const char *url, char *out_buf, int max_len, int *out_status);

#endif
