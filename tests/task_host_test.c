#include <stdio.h>
#define FLAIROS_HOST_TEST 1
#include "../kernel/task.c"

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static void dummy_fn(void) {}

int main(void) {
    task_init();
    CHECK(task_current_pid() == 1);
    CHECK(task_active_count() == 1);

    int p2 = task_spawn_kernel("worker1", dummy_fn);
    int p3 = task_spawn_kernel("worker2", dummy_fn);
    CHECK(p2 == 2 && p3 == 3);
    CHECK(task_active_count() == 3);

    /* Yield switches round-robin: 1 -> 2 -> 3 -> 1 */
    task_yield();
    CHECK(task_current_pid() == 2);
    task_yield();
    CHECK(task_current_pid() == 3);
    task_yield();
    CHECK(task_current_pid() == 1);

    /* Sleep task 2 and wake it via task_tick */
    task_yield();
    CHECK(task_current_pid() == 2);
    task_sleep_ticks(10, 5);
    CHECK(task_get(2)->state == TASK_SLEEPING);
    task_tick(14);
    CHECK(task_get(2)->state == TASK_SLEEPING);
    task_tick(15);
    CHECK(task_get(2)->state == TASK_READY);

    /* Kill and reap task 3 */
    CHECK(task_kill(3) == 0);
    int code = 0;
    CHECK(task_wait(3, &code) == 0);
    CHECK(code == -1);
    CHECK(task_get(3) == 0);

    /* Futex wait/wake test */
    uint32_t fut = 1;
    CHECK(task_futex_wait((uintptr_t)&fut, 2) == -1); /* mismatch */
    CHECK(task_futex_wake((uintptr_t)&fut, 1) == 0);
    /* Put task 2 into BLOCKED state on futex and wake it */
    tasks[1].state = TASK_BLOCKED;
    tasks[1].futex_addr = 0x1234u;
    CHECK(task_futex_wake(0x1234u, 1) == 1);
    CHECK(tasks[1].state == TASK_READY);

    int ut = task_spawn_user_thread("ui_thread", 0x200000u, 0x40010u, 0x7E000u, 0x10001000u);
    CHECK(ut > 0 && task_get(ut)->cr3 == 0x200000u && task_get(ut)->tls_base == 0x10001000u);

    puts("Task scheduler host test: all checks passed");
    return 0;
}
