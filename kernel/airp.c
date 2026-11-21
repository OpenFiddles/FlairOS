#include "textile.h"
#include "../include/airp.h"

typedef unsigned char u8;
typedef unsigned int u32;

#define AIRP_LOAD_OK          0
#define AIRP_LOAD_TOO_SHORT  -1
#define AIRP_LOAD_BAD_MAGIC  -2
#define AIRP_LOAD_VERSION    -3
#define AIRP_LOAD_HEADER     -4
#define AIRP_LOAD_API        -5
#define AIRP_LOAD_BASE       -6
#define AIRP_LOAD_SIZE       -7
#define AIRP_LOAD_ENTRY      -8
#define AIRP_LOAD_IMAGE      -9

/* Copies a .airp file's image to TX_APP_BASE and stores its entry address in *entry.
   The caller must not be running code from that slot. */
int airp_load(const u8 *file, u32 len, u32 *entry) {
    const struct airp_header *h;
    u8 *dst = (u8 *)TX_APP_BASE;
    u32 i, image_size, entry_value;

    if (len < sizeof(struct airp_header))
        return AIRP_LOAD_TOO_SHORT;
    h = (const struct airp_header *)file;
    if (h->magic != AIRP_MAGIC)
        return AIRP_LOAD_BAD_MAGIC;
    if (h->version != AIRP_VERSION)
        return AIRP_LOAD_VERSION;
    if (h->header_size != sizeof(struct airp_header))
        return AIRP_LOAD_HEADER;
    if (h->api_level > AIRP_API_LEVEL)
        return AIRP_LOAD_API;
    if (h->image_base != TX_APP_BASE)
        return AIRP_LOAD_BASE;
    if (h->image_size < 8 || h->image_size > AIRP_IMAGE_MAX)
        return AIRP_LOAD_SIZE;
    image_size = h->image_size;
    entry_value = h->entry;
    if (image_size > len - sizeof(struct airp_header))
        return AIRP_LOAD_SIZE;
    if (entry_value < TX_APP_BASE || entry_value - TX_APP_BASE >= image_size)
        return AIRP_LOAD_ENTRY;

    /* Source may live at 0x60000 inside the app/stack slot. Snapshot header
       fields before copying/clearing so the clear cannot destroy the header. */
    for (i = 0; i < image_size; i++)
        dst[i] = file[sizeof(struct airp_header) + i];

    /* The .airp file carries no BSS size, so clear the rest of the slot: .bss and any
       bytes left by a previous app read as zero. */
    for (i = image_size; i < AIRP_SLOT_SIZE; i++)
        dst[i] = 0;

    {
        const u32 *head = (const u32 *)TX_APP_BASE;
        if (head[0] != TX_APP_MAGIC || head[1] != entry_value)
            return AIRP_LOAD_IMAGE;
    }

    *entry = entry_value;
    return AIRP_LOAD_OK;
}
