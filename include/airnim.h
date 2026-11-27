#ifndef AIRNIM_H
#define AIRNIM_H

#include <stdint.h>
#include "airgraph.h"

/* Airnimation: the FlairOS animation engine.

   Widgets in SprunkUI are redrawn every frame, so animation is expressed as
   "tracked values": each animated thing has an id, and every frame it asks
   an_track(id, target, ...) for the value it should show right now. When the
   target changes the value glides to it; when animations are disabled it
   snaps. Any UI code (SprunkUI widgets, window chrome, apps) can use it.

   Time is measured in kernel ticks (100 Hz). Values are plain integers; the
   usual range for "progress" is 0..AN_ONE.

   Compositing effects (fades) go through AirGraph. When a backend that really
   implements blend_rect is registered and the user allows it, the GPU backend
   draws them; otherwise the CPU renderer does. Nothing else changes. */

#define AN_ONE 1000

enum an_ease { AN_LINEAR = 0, AN_OUT_CUBIC = 1, AN_IN_OUT = 2, AN_OUT_BACK = 3 };

typedef uint32_t an_id;

/* Stable ids. Use a name for global things, a position for list items. */
an_id an_id_str(const char *name);
an_id an_id_at(an_id base, int32_t x, int32_t y);

/* Engine control (Settings and the quick panel use these). */
void an_init(const struct ag_device *device);   /* detects the compositing backend */
void an_set_enabled(int enabled);
int an_enabled(void);
void an_set_gpu_allowed(int allowed);
int an_gpu_allowed(void);
int an_gpu_present(void);        /* a backend with real blend support exists */
int an_using_gpu(void);          /* present and allowed */
const char *an_backend_name(void);

/* Tracked value: call every frame. The first call returns target at once. */
int32_t an_track(an_id id, int32_t target, uint32_t duration_ticks, enum an_ease ease);
/* One-shot tween, for things like a window opening. */
void an_play(an_id id, int32_t from, int32_t to, uint32_t duration_ticks, enum an_ease ease);
int32_t an_value(an_id id, int32_t if_missing);
int an_busy(an_id id);           /* 1 while this id is moving */
int an_any_busy(void);

/* Helpers. t is 0..AN_ONE. */
int32_t an_lerp(int32_t a, int32_t b, int32_t t);
uint32_t an_mix_color(uint32_t a, uint32_t b, int32_t t);
/* Fade or tint a rectangle (alpha 0..255) via the selected backend. */
void an_blend_rect(struct ag_device *device, struct ag_surface *surface,
                   int32_t x, int32_t y, int32_t w, int32_t h,
                   uint32_t rgb, uint32_t alpha);


/* Spring physics & FlowDrip wireless transport telemetry (Wi-Fi / Bluetooth / FlowDrip) */
struct an_spring {
    int32_t pos;
    int32_t vel;
    int32_t target;
    int32_t stiffness; /* per-tick gain in permille */
    int32_t damping;   /* per-tick retention in permille (0..1000) */
};

void an_spring_init(struct an_spring *sp, int32_t initial, int32_t stiffness, int32_t damping);
int32_t an_spring_step(struct an_spring *sp, int32_t target);
int an_spring_settled(const struct an_spring *sp);
int32_t an_slide_offset(an_id id, int32_t open_state, int32_t span, uint32_t dur_ticks, enum an_ease ease);

/* ---- Preset animations -------------------------------------------------
   Named motion so callers do not re-pick durations and curves per call site.
   `from`/`to` are progress endpoints in the usual 0..AN_ONE space; a fade
   multiplies colour alpha by that progress, an offset preset subtracts it
   from a span. Add a preset here (and a host check) rather than teaching a
   shell a new curve. */
enum an_edge {
    AN_EDGE_NONE = 0,
    AN_EDGE_LEFT,
    AN_EDGE_RIGHT,
    AN_EDGE_TOP,
    AN_EDGE_BOTTOM
};

enum an_kind { AN_KIND_FADE = 0, AN_KIND_SLIDE, AN_KIND_POP, AN_KIND_CROSSFADE };

/* Distance is authored as a fraction of the element's own extent, so the
   preset composes with AirGraph's dynamic viewport scaling without multiplying
   that fraction by the panel scale a second time. */
#define AN_PRESET_COMPOSE_VIEWPORT 0x1u

struct an_preset {
    const char *name;
    enum an_kind kind;
    int32_t from, to;          /* progress endpoints, 0..AN_ONE */
    uint32_t duration_ticks;    /* 100 Hz ticks */
    enum an_ease ease;
    enum an_edge edge;          /* direction for slides */
    int32_t distance_pm;        /* permille of the element's own extent */
    uint32_t flags;             /* AN_PRESET_COMPOSE_VIEWPORT */
};

/* Resolved motion for one frame. dx/dy are offsets from the element's resting
   position, alpha is 0..AN_ONE, scale_pm is 1000 at rest, and alpha_a/alpha_b
   are the two content-screen weights for a cross-fade. */
struct an_motion {
    int32_t dx, dy;
    int32_t alpha;
    int32_t scale_pm;
    int32_t alpha_a, alpha_b;
};

int an_preset_count(void);
const struct an_preset *an_preset_at(int index);
const struct an_preset *an_preset_lookup(const char *name);
/* 1 when a named preset exists, 0 for NULL/empty/unknown names. */
int an_preset_exists(const char *name);
/* Play by name. Returns 0, or -1 for an unknown name (engine untouched). */
int an_preset_play(an_id id, const char *name);
int an_preset_play_at(an_id id, const struct an_preset *preset);
/* 1 once nothing is moving on this id (including before the first play),
   0 while the requested preset is still producing in-between values. */
int an_preset_finished(an_id id);
/* Current progress, clamped to 0..AN_ONE. */
int32_t an_preset_value(an_id id, const struct an_preset *preset);
/* Offset for a slide preset: `span` at progress 0, 0 at AN_ONE. */
int32_t an_preset_offset(an_id id, const struct an_preset *preset, int32_t span);

/* Resolve a preset for one frame. `progress` is its current an_preset_value().
   w/h are the element's own size, so distance is a fraction of the element;
   viewport_scale_q16 is only consulted by AN_PRESET_COMPOSE_VIEWPORT. */
void an_preset_motion(const struct an_preset *preset, int32_t progress,
                      int32_t w, int32_t h, uint32_t viewport_scale_q16,
                      struct an_motion *out);
/* Apply a preset to a rectangle in a viewport: resolves the motion from the
   rectangle's own size and clamps it inside the viewport. Returns 0, or -1 for
   a missing preset/motion. */
int an_preset_frame(const struct an_preset *preset, int32_t progress,
                    int32_t x, int32_t y, int32_t w, int32_t h,
                    int32_t viewport_w, int32_t viewport_h,
                    uint32_t viewport_scale_q16, struct an_motion *out);
/* Clamp a motion so the element's rectangle stays inside the viewport. */
int an_motion_clamp(struct an_motion *motion, int32_t x, int32_t y,
                    int32_t w, int32_t h, int32_t viewport_w, int32_t viewport_h);
/* Cross-fade two content screens: draw `from_rgb`, then `to_rgb` at the
   preset's progress. The midpoint is a real pixel blend, not an instant swap. */
void an_crossfade_rect(struct ag_device *device, struct ag_surface *surface,
                       int32_t x, int32_t y, int32_t w, int32_t h,
                       uint32_t from_rgb, uint32_t to_rgb, int32_t progress);

enum flowdrip_link {
    FLOWDRIP_LINK_NONE = 0,
    FLOWDRIP_LINK_WIFI = 1,
    FLOWDRIP_LINK_BLUETOOTH = 2,
    FLOWDRIP_LINK_DUAL = 3
};

struct flowdrip_peer {
    char name[24];
    enum flowdrip_link link;
    int signal_pct;
    int ready;
};

#endif
