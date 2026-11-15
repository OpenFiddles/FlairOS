/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef FLAIROS_MM_CORE_H
#define FLAIROS_MM_CORE_H
struct mm_frame_allocator {
    unsigned char *available;
    unsigned char *managed;
    unsigned char *reserved;
    unsigned int base;
    unsigned int frame_count;
    unsigned int free_count;
    unsigned int managed_count;
    unsigned int reserved_count;
    unsigned int hint;
};
void mm_frames_init(struct mm_frame_allocator *m, unsigned char *available,
                    unsigned char *managed, unsigned char *reserved,
                    unsigned int base, unsigned int frame_count);
unsigned int mm_frames_add_range(struct mm_frame_allocator *m,
                                 unsigned int base, unsigned int length);
unsigned int mm_frames_alloc(struct mm_frame_allocator *m);
unsigned int mm_frames_alloc_contiguous(struct mm_frame_allocator *m,
                                        unsigned int count);
int mm_frames_free(struct mm_frame_allocator *m, unsigned int address);
int mm_frames_reserve(struct mm_frame_allocator *m, unsigned int base,
                      unsigned int length);
#endif
