#include <stdio.h>
#include <string.h>
#define FLAIROS_HOST_TEST 1
#include "../kernel/binder.c"

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void) {
    CHECK(binder_init() == 0);
    uint32_t h = 0;
    CHECK(binder_register("SurfaceFlinger", 2, &h) == 0 && h > 0);
    CHECK(binder_register("SurfaceFlinger", 3, 0) != 0);

    uint32_t h2 = 0; int pid = 0;
    CHECK(binder_lookup("SurfaceFlinger", &h2, &pid) == 0 && h2 == h && pid == 2);

    const char *msg = "CREATE_SURFACE";
    CHECK(binder_transact(4, h, 1001, msg, (uint32_t)strlen(msg) + 1) == 0);

    struct binder_msg rx;
    CHECK(binder_recv(2, &rx) == 1);
    CHECK(rx.sender_pid == 4 && rx.code == 1001);
    CHECK(strcmp((const char *)rx.data, msg) == 0);
    CHECK(binder_recv(2, &rx) == 0);

    puts("Binder IPC host test: all checks passed");
    return 0;
}
