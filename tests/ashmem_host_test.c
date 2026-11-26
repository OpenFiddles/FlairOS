#include <stdio.h>
#include <string.h>
#define FLAIROS_HOST_TEST 1
#include "../kernel/mm/ashmem.c"

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void) {
    CHECK(ashmem_init() == 0);
    int id = ashmem_create("fb_surface", 4096);
    CHECK(id > 0);
    CHECK(ashmem_find("fb_surface") == id);

    uint32_t sz = 0;
    uint8_t *p1 = (uint8_t *)ashmem_map(id, &sz);
    CHECK(p1 != 0 && sz == 4096);
    p1[0] = 0x5Au;

    uint8_t *p2 = (uint8_t *)ashmem_map(id, 0);
    CHECK(p2 == p1 && p2[0] == 0x5Au);

    CHECK(ashmem_release(id) == 0);
    CHECK(ashmem_release(id) == 0);
    CHECK(ashmem_release(id) == 0);
    CHECK(ashmem_find("fb_surface") == -1);

    puts("ashmem shared memory host test: all checks passed");
    return 0;
}
