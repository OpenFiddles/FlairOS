#include "../../include/airnim.h"

extern volatile unsigned int ticks;

#define AN_SLOTS 32u

struct an_slot {
    an_id id;
    int32_t from, to;
    uint32_t start, dur, touched;
    uint8_t used, ease;
};

#ifdef FLAIROS_HOST_TEST
static struct an_slot host_slots[AN_SLOTS];
static struct an_slot *get_slots(void) { return host_slots; }
#else
extern void *kmalloc(unsigned int size);
static struct an_slot *slots;
static struct an_slot *get_slots(void) {
    if (!slots) {
        slots = (struct an_slot *)kmalloc(sizeof(struct an_slot) * AN_SLOTS);
        if (slots) {
            unsigned char *p = (unsigned char *)slots;
            for (unsigned int i = 0; i < sizeof(struct an_slot) * AN_SLOTS; i++) p[i] = 0;
        }
    }
    return slots;
}
#endif
static uint8_t g_enabled = 1u, g_gpu_allowed = 1u, g_gpu_present;
static const char *g_renderer = "cpu";

static int32_t ease_apply(uint8_t ease, int32_t t) {
    int32_t u, u2, u3, a;
    if (t <= 0) return 0;
    if (t >= AN_ONE && ease != AN_OUT_BACK) return AN_ONE;
    switch (ease) {
    case AN_OUT_CUBIC:
        u = AN_ONE - t;
        return AN_ONE - ((u * u / AN_ONE) * u / AN_ONE);
    case AN_IN_OUT:
        a = t * t / AN_ONE;
        return a * (3 * AN_ONE - 2 * t) / AN_ONE;
    case AN_OUT_BACK:
        if (t >= AN_ONE) return AN_ONE;
        u = t - AN_ONE;                 /* -1000..0 */
        u2 = u * u / AN_ONE;
        u3 = u2 * u / AN_ONE;
        return AN_ONE + (2702 * u3 + 1702 * u2) / AN_ONE;
    default:
        return t;
    }
}

static int32_t slot_value(const struct an_slot *s, uint32_t now) {
    uint32_t elapsed;
    if (!g_enabled || s->dur == 0u) return s->to;
    elapsed = now - s->start;
    if (elapsed >= s->dur) return s->to;
    return s->from + (s->to - s->from) *
           ease_apply(s->ease, (int32_t)(elapsed * AN_ONE / s->dur)) / AN_ONE;
}

static int slot_busy(const struct an_slot *s, uint32_t now) {
    return s->used && g_enabled && s->dur != 0u && (now - s->start) < s->dur;
}

static struct an_slot *slot_find(an_id id, int create, int *created) {
    uint32_t now = ticks, i, best = 0u, best_age = 0u;
    int have_best = 0;
    if (created) *created = 0;
    for (i = 0; i < AN_SLOTS; ++i)
        if (get_slots()[i].used && get_slots()[i].id == id) return &get_slots()[i];
    if (!create) return 0;
    for (i = 0; i < AN_SLOTS; ++i)
        if (!get_slots()[i].used) { best = i; have_best = 1; break; }
    if (!have_best) {
        /* Evict the least recently touched idle slot, else the oldest busy one. */
        for (i = 0; i < AN_SLOTS; ++i) {
            uint32_t age = now - get_slots()[i].touched;
            if (slot_busy(&get_slots()[i], now)) age >>= 1;
            if (!have_best || age > best_age) { best = i; best_age = age; have_best = 1; }
        }
    }
    get_slots()[best].used = 1u;
    get_slots()[best].id = id;
    get_slots()[best].dur = 0u;
    get_slots()[best].touched = now;
    if (created) *created = 1;
    return &get_slots()[best];
}

an_id an_id_str(const char *name) {
    uint32_t h = 2166136261u;
    while (name && *name) { h ^= (uint8_t)*name++; h *= 16777619u; }
    return h;
}

an_id an_id_at(an_id base, int32_t x, int32_t y) {
    uint32_t h = base * 16777619u;
    h ^= (uint32_t)x * 73856093u;
    h ^= (uint32_t)y * 19349663u;
    return h;
}

void an_init(const struct ag_device *device) {
    struct ag_resource_info info;
    g_gpu_present = 0u;
    g_renderer = "cpu";
    /* A GPU backend only counts when it really implements blending; a probe
       that merely names a device does not make animations faster. */
    if (device && ag_get_resource_info(device, &info) == 0 &&
        info.gpu_available && info.gpu_accelerated &&
        device->backend && device->backend->blend_rect) {
        g_gpu_present = 1u;
        g_renderer = info.renderer_name ? info.renderer_name : "gpu";
    }
}

void an_set_enabled(int enabled) { g_enabled = enabled ? 1u : 0u; }
int an_enabled(void) { return g_enabled; }
void an_set_gpu_allowed(int allowed) { g_gpu_allowed = allowed ? 1u : 0u; }
int an_gpu_allowed(void) { return g_gpu_allowed; }
int an_gpu_present(void) { return g_gpu_present; }
int an_using_gpu(void) { return g_gpu_present && g_gpu_allowed; }
const char *an_backend_name(void) { return an_using_gpu() ? g_renderer : "cpu"; }

int32_t an_track(an_id id, int32_t target, uint32_t duration_ticks, enum an_ease ease) {
    uint32_t now = ticks;
    int created = 0;
    struct an_slot *s = slot_find(id, 1, &created);
    s->touched = now;
    if (created || !g_enabled) {
        s->from = s->to = target;
        s->dur = 0u;
        s->start = now;
        return target;
    }
    if (s->to != target) {
        s->from = slot_value(s, now);
        s->to = target;
        s->start = now;
        s->dur = duration_ticks ? duration_ticks : 1u;
        s->ease = (uint8_t)ease;
    }
    return slot_value(s, now);
}

void an_play(an_id id, int32_t from, int32_t to, uint32_t duration_ticks, enum an_ease ease) {
    uint32_t now = ticks;
    struct an_slot *s = slot_find(id, 1, 0);
    s->touched = now;
    s->from = g_enabled ? from : to;
    s->to = to;
    s->start = now;
    s->dur = (g_enabled && duration_ticks) ? duration_ticks : 0u;
    s->ease = (uint8_t)ease;
}

int32_t an_value(an_id id, int32_t if_missing) {
    struct an_slot *s = slot_find(id, 0, 0);
    return s ? slot_value(s, ticks) : if_missing;
}

int an_busy(an_id id) {
    struct an_slot *s = slot_find(id, 0, 0);
    return s ? slot_busy(s, ticks) : 0;
}

int an_any_busy(void) {
    uint32_t i, now = ticks;
    for (i = 0; i < AN_SLOTS; ++i)
        if (slot_busy(&get_slots()[i], now)) return 1;
    return 0;
}

int32_t an_lerp(int32_t a, int32_t b, int32_t t) {
    if (t <= 0) return a;
    if (t >= AN_ONE) return b;
    return a + (b - a) * t / AN_ONE;
}

uint32_t an_mix_color(uint32_t a, uint32_t b, int32_t t) {
    uint32_t out = 0u;
    int shift;
    for (shift = 0; shift < 32; shift += 8) {
        int32_t ca = (int32_t)((a >> shift) & 0xFFu);
        int32_t cb = (int32_t)((b >> shift) & 0xFFu);
        out |= (uint32_t)an_lerp(ca, cb, t) << shift;
    }
    return out;
}

void an_blend_rect(struct ag_device *device, struct ag_surface *surface,
                   int32_t x, int32_t y, int32_t w, int32_t h,
                   uint32_t rgb, uint32_t alpha) {
    if (!surface || w <= 0 || h <= 0 || alpha == 0u) return;
    if (alpha > 255u) alpha = 255u;
    ag_blend_rect(an_using_gpu() ? device : 0, surface, x, y, w, h,
                  (alpha << 24) | (rgb & 0x00FFFFFFu));
}

void an_spring_init(struct an_spring *sp, int32_t initial, int32_t stiffness, int32_t damping) {
    if (!sp) return;
    sp->pos = initial;
    sp->vel = 0;
    sp->target = initial;
    sp->stiffness = stiffness > 0 ? stiffness : 220;
    sp->damping = (damping >= 0 && damping <= 1000) ? damping : 760;
}

int32_t an_spring_step(struct an_spring *sp, int32_t target) {
    int32_t err, accel;
    if (!sp) return target;
    sp->target = target;
    if (!g_enabled) {
        sp->pos = target;
        sp->vel = 0;
        return sp->pos;
    }
    err = sp->target - sp->pos;
    accel = (err * sp->stiffness) / 1000;
    sp->vel = ((sp->vel + accel) * sp->damping) / 1000;
    sp->pos += sp->vel;
    if (an_spring_settled(sp)) {
        sp->pos = sp->target;
        sp->vel = 0;
    }
    return sp->pos;
}

int an_spring_settled(const struct an_spring *sp) {
    int32_t diff, spd;
    if (!sp) return 1;
    diff = sp->target - sp->pos;
    if (diff < 0) diff = -diff;
    spd = sp->vel < 0 ? -sp->vel : sp->vel;
    return diff <= 1 && spd <= 1;
}

int32_t an_slide_offset(an_id id, int32_t open_state, int32_t span, uint32_t dur_ticks, enum an_ease ease) {
    int32_t p = an_track(id, open_state ? AN_ONE : 0, dur_ticks, ease);
    if (p < 0) p = 0;
    if (p > AN_ONE) p = AN_ONE;
    return span - (span * p) / AN_ONE;
}

/* Named motion. Durations are in 100 Hz ticks; curves reuse the engine's
   easing so a preset cannot drift from the code path it animates. */
static const struct an_preset g_presets[] = {
    { "fade-in",  0,      AN_ONE, 12, AN_OUT_CUBIC },
    { "fade-out", AN_ONE, 0,      10, AN_IN_OUT },
    { "slide-in", 0,      AN_ONE, 16, AN_OUT_CUBIC },
    { "slide-out", AN_ONE, 0,     14, AN_IN_OUT },
    { "pop",      0,      AN_ONE, 14, AN_OUT_BACK },
    { "settle",   0,      AN_ONE, 18, AN_IN_OUT },
};

int an_preset_count(void) {
    return (int)(sizeof(g_presets) / sizeof(g_presets[0]));
}

const struct an_preset *an_preset_at(int index) {
    if (index < 0 || index >= an_preset_count()) return 0;
    return &g_presets[index];
}

static int an_name_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

const struct an_preset *an_preset_lookup(const char *name) {
    int i;
    if (!name) return 0;
    for (i = 0; i < an_preset_count(); ++i)
        if (an_name_eq(g_presets[i].name, name)) return &g_presets[i];
    return 0;
}

int an_preset_play_at(an_id id, const struct an_preset *preset) {
    if (!preset) return -1;
    an_play(id, preset->from, preset->to, preset->duration_ticks, preset->ease);
    return 0;
}

int an_preset_play(an_id id, const char *name) {
    const struct an_preset *preset = an_preset_lookup(name);
    if (!preset) return -1;
    return an_preset_play_at(id, preset);
}

int32_t an_preset_value(an_id id, const struct an_preset *preset) {
    int32_t v = an_value(id, preset ? preset->to : 0);
    if (v < 0) v = 0;
    if (v > AN_ONE) v = AN_ONE;
    return v;
}

int32_t an_preset_offset(an_id id, const struct an_preset *preset, int32_t span) {
    int32_t p;
    if (span <= 0) return 0;
    p = an_preset_value(id, preset);
    return span - (span * p) / AN_ONE;
}
