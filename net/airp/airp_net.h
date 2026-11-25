#ifndef FLAIROS_AIRP_NET_H
#define FLAIROS_AIRP_NET_H

#include <stdint.h>

#define AIRP_MAX_PEERS 6
#define AIRP_PROTO_VER 2

enum airp_role {
    AIRP_ROLE_SENDER   = 1,
    AIRP_ROLE_RECEIVER = 2,
    AIRP_ROLE_DUPLEX   = 3
};

enum airp_session_state {
    AIRP_STATE_IDLE        = 0,
    AIRP_STATE_DISCOVERING = 1,
    AIRP_STATE_PAIRING     = 2,
    AIRP_STATE_CONNECTED   = 3,
    AIRP_STATE_STREAMING   = 4,
    AIRP_STATE_ERROR       = 5
};

enum airp_payload_type {
    AIRP_PAY_MEDIA  = 1,
    AIRP_PAY_SCREEN = 2,
    AIRP_PAY_FILE   = 3,
    AIRP_PAY_STATE  = 4
};

struct airp_peer {
    int used;
    char name[32];
    char arch[16];
    char form_factor[16];
    uint32_t pin_hash;
    int paired;
};

struct airp_session {
    enum airp_role role;
    enum airp_session_state state;
    enum airp_payload_type payload_type;
    int active_peer;
    uint32_t session_token;
    uint32_t bytes_transferred;
    uint32_t frames_streamed;
    char local_name[32];
    char status_text[64];
    char last_error[48];
};

void airp_net_init(const char *local_name, enum airp_role role);
int airp_discover_start(void);
int airp_peer_count(void);
const struct airp_peer *airp_peer_get(int idx);
int airp_pair_peer(int peer_idx, uint32_t pin);
int airp_start_stream(enum airp_payload_type type, const char *label, uint32_t bytes);
int airp_send_frame(uint32_t frame_bytes);
void airp_inject_error(const char *reason);
void airp_recover(void);
void airp_disconnect(void);
const struct airp_session *airp_session_status(void);
const char *airp_state_name(enum airp_session_state st);

#endif
