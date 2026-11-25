/* SPDX-License-Identifier: AGPL-3.0-only */
#include "airp_net.h"

static struct airp_peer peers[AIRP_MAX_PEERS];
static struct airp_session sess;

static void scpy(char *d, const char *s, int cap) {
    int i = 0;
    if (!d || cap <= 0) return;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static uint32_t hash_pin(uint32_t pin) {
    uint32_t x = pin ^ 0xA1B2C3D4u;
    x = ((x >> 16) ^ x) * 0x45D9F3Bu;
    return x;
}

static void add_peer(int idx, const char *name, const char *arch, const char *ff, uint32_t pin) {
    if (idx < 0 || idx >= AIRP_MAX_PEERS) return;
    peers[idx].used = 1;
    scpy(peers[idx].name, name, sizeof(peers[idx].name));
    scpy(peers[idx].arch, arch, sizeof(peers[idx].arch));
    scpy(peers[idx].form_factor, ff, sizeof(peers[idx].form_factor));
    peers[idx].pin_hash = hash_pin(pin);
    peers[idx].paired = 0;
}

void airp_net_init(const char *local_name, enum airp_role role) {
    for (int i = 0; i < AIRP_MAX_PEERS; i++) peers[i].used = 0;
    sess.role = role;
    sess.state = AIRP_STATE_IDLE;
    sess.payload_type = AIRP_PAY_SCREEN;
    sess.active_peer = -1;
    sess.session_token = 0;
    sess.bytes_transferred = 0;
    sess.frames_streamed = 0;
    sess.last_error[0] = 0;
    scpy(sess.local_name, local_name ? local_name : "FlairOS-Node", sizeof(sess.local_name));
    scpy(sess.status_text,
         role == AIRP_ROLE_RECEIVER ? "AirP Receiver Listening" : "AirP Idle (Click Scan)",
         sizeof(sess.status_text));
}

int airp_discover_start(void) {
    sess.state = AIRP_STATE_DISCOVERING;
    add_peer(0, sess.local_name, "local", "loopback", 4242u);
    scpy(sess.status_text, "1 Local Loopback Peer Ready", sizeof(sess.status_text));
    return 1;
}

int airp_peer_count(void) {
    int c = 0;
    for (int i = 0; i < AIRP_MAX_PEERS; i++) if (peers[i].used) c++;
    return c;
}

const struct airp_peer *airp_peer_get(int idx) {
    if (idx < 0 || idx >= AIRP_MAX_PEERS || !peers[idx].used) return 0;
    return &peers[idx];
}

int airp_pair_peer(int peer_idx, uint32_t pin) {
    if (peer_idx < 0 || peer_idx >= AIRP_MAX_PEERS || !peers[peer_idx].used) {
        airp_inject_error("Peer not found");
        return -1;
    }
    sess.state = AIRP_STATE_PAIRING;
    if (peers[peer_idx].pin_hash != hash_pin(pin)) {
        airp_inject_error("Pairing PIN mismatch");
        return -2;
    }
    peers[peer_idx].paired = 1;
    sess.active_peer = peer_idx;
    sess.session_token = 0x41525000u | (uint32_t)(peer_idx + 1);
    sess.state = AIRP_STATE_CONNECTED;
    sess.last_error[0] = 0;
    scpy(sess.status_text, "Paired: ", sizeof(sess.status_text));
    int i = 0;
    while (sess.status_text[i]) i++;
    const char *nm = peers[peer_idx].name;
    int j = 0;
    while (nm[j] && i + 1 < (int)sizeof(sess.status_text)) sess.status_text[i++] = nm[j++];
    sess.status_text[i] = 0;
    return 0;
}

int airp_start_stream(enum airp_payload_type type, const char *label, uint32_t bytes) {
    if (sess.state != AIRP_STATE_CONNECTED && sess.state != AIRP_STATE_STREAMING) {
        airp_inject_error("No active AirP peer");
        return -1;
    }
    sess.payload_type = type;
    sess.state = AIRP_STATE_STREAMING;
    sess.bytes_transferred += bytes;
    sess.frames_streamed++;
    scpy(sess.status_text, label ? label : "AirP Streaming", sizeof(sess.status_text));
    return 0;
}

int airp_send_frame(uint32_t frame_bytes) {
    if (sess.state != AIRP_STATE_STREAMING) return -1;
    sess.bytes_transferred += frame_bytes;
    sess.frames_streamed++;
    return 0;
}

void airp_inject_error(const char *reason) {
    sess.state = AIRP_STATE_ERROR;
    scpy(sess.last_error, reason ? reason : "AirP transport error", sizeof(sess.last_error));
    scpy(sess.status_text, sess.last_error, sizeof(sess.status_text));
}

void airp_recover(void) {
    if (sess.state == AIRP_STATE_ERROR) {
        sess.last_error[0] = 0;
        sess.state = (sess.active_peer >= 0) ? AIRP_STATE_CONNECTED : AIRP_STATE_IDLE;
        scpy(sess.status_text,
             sess.state == AIRP_STATE_CONNECTED ? "Recovered AirP session" : "AirP Idle",
             sizeof(sess.status_text));
    }
}

void airp_disconnect(void) {
    sess.state = AIRP_STATE_IDLE;
    sess.active_peer = -1;
    sess.session_token = 0;
    scpy(sess.status_text, "AirP Disconnected", sizeof(sess.status_text));
}

const struct airp_session *airp_session_status(void) { return &sess; }

const char *airp_state_name(enum airp_session_state st) {
    switch (st) {
    case AIRP_STATE_IDLE: return "IDLE";
    case AIRP_STATE_DISCOVERING: return "DISCOVERING";
    case AIRP_STATE_PAIRING: return "PAIRING";
    case AIRP_STATE_CONNECTED: return "CONNECTED";
    case AIRP_STATE_STREAMING: return "STREAMING";
    case AIRP_STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}
