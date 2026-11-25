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
