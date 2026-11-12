#ifndef PMM_H
#define PMM_H

#define PMM_FRAME_SIZE 4096u

void serial_str(const char *s);

void pmm_init(void);
unsigned int pmm_alloc_frame(void);
unsigned int pmm_alloc_contig(unsigned int frames);
int pmm_free_frame(unsigned int addr);
unsigned int pmm_free_frames(void);
unsigned int pmm_managed_frames(void);
int pmm_selftest(void);

#endif
