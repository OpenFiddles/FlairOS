#include "../include/task.h"

#ifdef FLAIROS_HOST_TEST
#include <stdlib.h>
static void *task_alloc(unsigned int sz) { return malloc(sz); }
static void task_free(void *p) { free(p); }
static void task_irq_off(void) {}
static void task_irq_on(void) {}
static void task_switch_ctx(uint32_t *old_esp, uint32_t new_esp) {
    (void)old_esp;
    (void)new_esp;
}
static void task_set_tss_esp0(uint32_t esp0) { (void)esp0; }
static void task_log(const char *s) { (void)s; }
static uint32_t task_now_ticks(void) { return 0; }
static void task_halt_cpu(void) {}
#else
#include "../include/kheap.h"
#include "../include/hal.h"
void serial_str(const char *s);
void tss_set_esp0(unsigned int esp0);
unsigned int kernel_ticks(void);
void task_switch_asm(uint32_t *old_esp, uint32_t new_esp);
static void *task_alloc(unsigned int sz) { return kmalloc(sz); }
static void task_free(void *p) { kfree(p); }
static void task_irq_off(void) { hal_cpu_irq_disable(); }
static void task_irq_on(void) { hal_cpu_irq_enable(); }
static void task_switch_ctx(uint32_t *old_esp, uint32_t new_esp) {
    task_switch_asm(old_esp, new_esp);
}
static void task_set_tss_esp0(uint32_t esp0) { tss_set_esp0(esp0); }
static void task_log(const char *s) { serial_str(s); }
static uint32_t task_now_ticks(void) { return kernel_ticks(); }
static void task_halt_cpu(void) { hal_cpu_halt(); }
#endif

static struct task *tasks;
static int current_idx = -1;
static int next_pid = 1;
static int quantum_left = TASK_QUANTUM;
static volatile int sched_ready = 0;

static void copy_name(char *dst, const char *src) {
    int i = 0;
    if (src) {
        while (src[i] && i < TASK_NAME_MAX - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static int find_idx_by_pid(int pid) {
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state != TASK_UNUSED && tasks[i].pid == pid)
            return i;
    }
    return -1;
}

void task_init(void) {
    if (!tasks) tasks = (struct task *)task_alloc(sizeof(struct task) * TASK_MAX);
    if (!tasks) return;
    for (int i = 0; i < TASK_MAX; i++) {
        tasks[i].pid = 0;
        tasks[i].ppid = 0;
        tasks[i].state = TASK_UNUSED;
        tasks[i].esp = 0;
        tasks[i].kstack_base = 0;
        tasks[i].cr3 = 0;
        tasks[i].wake_tick = 0;
        tasks[i].futex_addr = 0;
        tasks[i].tls_base = 0;
        tasks[i].slices = 0;
        tasks[i].exit_code = 0;
        tasks[i].name[0] = 0;
    }
    tasks[0].pid = next_pid++;
    tasks[0].ppid = 0;
    tasks[0].state = TASK_RUNNING;
    copy_name(tasks[0].name, "kernel");
    current_idx = 0;
    quantum_left = TASK_QUANTUM;
    sched_ready = 1;
}

static void (*pending_entry[TASK_MAX])(void);

static void task_trampoline(void) {
    int idx = current_idx;
    void (*fn)(void) = (idx >= 0 && idx < TASK_MAX) ? pending_entry[idx] : 0;
    task_irq_on();
    if (fn) fn();
    task_exit(0);
}

int task_spawn_kernel(const char *name, void (*entry)(void)) {
    if (!sched_ready || !entry) return -1;
    task_irq_off();
    int slot = -1;
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        task_irq_on();
        return -1;
    }
    uint8_t *stack = (uint8_t *)task_alloc(TASK_KSTACK_SZ);
    if (!stack) {
        task_irq_on();
        return -1;
    }
#ifdef FLAIROS_HOST_TEST
    uint32_t *sp = (uint32_t *)(stack + TASK_KSTACK_SZ);
    (void)task_trampoline;
#else
    uintptr_t top = (uintptr_t)(stack + TASK_KSTACK_SZ);
    top &= ~(uintptr_t)0xFu;
    uint32_t *sp = (uint32_t *)top;
    *(--sp) = (uint32_t)(uintptr_t)task_trampoline; /* ret eip */
    *(--sp) = 0; /* ebp */
    *(--sp) = 0; /* ebx */
    *(--sp) = 0; /* esi */
    *(--sp) = 0; /* edi */
#endif

    tasks[slot].pid = next_pid++;
    tasks[slot].ppid = (current_idx >= 0) ? tasks[current_idx].pid : 0;
    tasks[slot].state = TASK_READY;
    tasks[slot].esp = (uint32_t)(uintptr_t)sp;
    tasks[slot].kstack_base = (uintptr_t)stack;
    tasks[slot].cr3 = 0;
    tasks[slot].wake_tick = 0;
    tasks[slot].futex_addr = 0;
    tasks[slot].tls_base = 0;
    tasks[slot].slices = 0;
    tasks[slot].exit_code = 0;
    copy_name(tasks[slot].name, name);
    pending_entry[slot] = entry;
    int pid = tasks[slot].pid;
    task_irq_on();
    return pid;
}

static int pick_next_ready(void) {
    for (int step = 1; step <= TASK_MAX; step++) {
        int idx = (current_idx + step) % TASK_MAX;
        if (tasks[idx].state == TASK_READY)
            return idx;
    }
    return -1;
}

static void schedule_locked(void) {
    if (!sched_ready || current_idx < 0) return;
    int next = pick_next_ready();
    if (next < 0) {
        if (tasks[current_idx].state == TASK_RUNNING)
            tasks[current_idx].slices++;
        return;
    }
    int prev = current_idx;
    if (tasks[prev].state == TASK_RUNNING)
        tasks[prev].state = TASK_READY;
    tasks[next].state = TASK_RUNNING;
    tasks[next].slices++;
    current_idx = next;
    quantum_left = TASK_QUANTUM;
    if (tasks[next].kstack_base)
        task_set_tss_esp0((uint32_t)(tasks[next].kstack_base + TASK_KSTACK_SZ));
    task_switch_ctx(&tasks[prev].esp, tasks[next].esp);
}

void task_yield(void) {
    if (!sched_ready) return;
    task_irq_off();
    schedule_locked();
    task_irq_on();
}

void task_sleep_ticks(uint32_t now_ticks, uint32_t n) {
    if (!sched_ready || current_idx < 0) return;
    if (n == 0) {
        task_yield();
        return;
    }
    task_irq_off();
    tasks[current_idx].wake_tick = now_ticks + n;
    tasks[current_idx].state = TASK_SLEEPING;
    schedule_locked();
    task_irq_on();
}

void task_exit(int code) {
    if (!sched_ready || current_idx <= 0) return;
    task_irq_off();
    tasks[current_idx].exit_code = code;
    tasks[current_idx].state = TASK_ZOMBIE;
    schedule_locked();
    task_irq_on();
    for (;;) task_halt_cpu();
}

int task_wait(int pid, int *exit_code) {
    if (!sched_ready) return -1;
    for (;;) {
        task_irq_off();
        int idx = find_idx_by_pid(pid);
        if (idx < 0) {
            task_irq_on();
            return -1;
        }
        if (tasks[idx].state == TASK_ZOMBIE) {
            if (exit_code) *exit_code = tasks[idx].exit_code;
            if (tasks[idx].kstack_base)
                task_free((void *)(uintptr_t)tasks[idx].kstack_base);
            tasks[idx].state = TASK_UNUSED;
            tasks[idx].pid = 0;
            tasks[idx].kstack_base = 0;
            task_irq_on();
            return 0;
        }
        task_irq_on();
#ifdef FLAIROS_HOST_TEST
        return 1; /* non-blocking in host test */
#else
        task_yield();
        if (pick_next_ready() < 0)
            task_halt_cpu();
#endif
    }
}

int task_kill(int pid) {
    if (!sched_ready || pid <= 1) return -1;
    task_irq_off();
    int idx = find_idx_by_pid(pid);
    if (idx <= 0) {
        task_irq_on();
        return -1;
    }
    tasks[idx].exit_code = -1;
    tasks[idx].state = TASK_ZOMBIE;
    if (idx == current_idx)
        schedule_locked();
    task_irq_on();
    return 0;
}

void task_tick(uint32_t now_ticks) {
    if (!sched_ready || current_idx < 0) return;
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_SLEEPING &&
            (int32_t)(now_ticks - tasks[i].wake_tick) >= 0) {
            tasks[i].state = TASK_READY;
        }
    }
    if (quantum_left > 0) quantum_left--;
    if (quantum_left == 0)
        schedule_locked();
}



int task_spawn_user_thread(const char *name, uint32_t cr3, uint32_t user_eip,
                           uint32_t user_esp, uint32_t tls_base) {
    if (!sched_ready || !user_eip || !user_esp) return -1;
    task_irq_off();
    int slot = -1;
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_UNUSED) { slot = i; break; }
    }
    if (slot < 0) { task_irq_on(); return -1; }
    uint8_t *stack = (uint8_t *)task_alloc(TASK_KSTACK_SZ);
    if (!stack) { task_irq_on(); return -1; }
    tasks[slot].pid = next_pid++;
    tasks[slot].ppid = (current_idx >= 0) ? tasks[current_idx].pid : 0;
    tasks[slot].state = TASK_READY;
    tasks[slot].kstack_base = (uintptr_t)stack;
    tasks[slot].cr3 = cr3;
    tasks[slot].tls_base = tls_base;
    tasks[slot].wake_tick = 0;
    tasks[slot].futex_addr = 0;
    tasks[slot].slices = 0;
    tasks[slot].exit_code = 0;
    copy_name(tasks[slot].name, name ? name : "uthread");
    (void)user_eip; (void)user_esp;
    int pid = tasks[slot].pid;
    task_irq_on();
    return pid;
}

int task_futex_wait(uintptr_t addr, uint32_t expected) {
    if (!sched_ready || current_idx < 0 || !addr) return -1;
    task_irq_off();
    uint32_t cur = *(volatile uint32_t *)addr;
    if (cur != expected) {
        task_irq_on();
        return -1; /* EAGAIN */
    }
    tasks[current_idx].futex_addr = addr;
    tasks[current_idx].state = TASK_BLOCKED;
    schedule_locked();
    task_irq_on();
    return 0;
}

int task_futex_wake(uintptr_t addr, int max_wake) {
    if (!sched_ready || !addr || max_wake <= 0) return 0;
    int woken = 0;
    task_irq_off();
    for (int i = 0; i < TASK_MAX && woken < max_wake; i++) {
        if (tasks[i].state == TASK_BLOCKED && tasks[i].futex_addr == addr) {
            tasks[i].futex_addr = 0;
            tasks[i].state = TASK_READY;
            woken++;
        }
    }
    task_irq_on();
    return woken;
}

int task_current_pid(void) {
    return (current_idx >= 0 && current_idx < TASK_MAX) ? tasks[current_idx].pid : 0;
}

int task_active_count(void) {
    int n = 0;
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_ZOMBIE)
            n++;
    }
    return n;
}

const struct task *task_get(int pid) {
    int idx = find_idx_by_pid(pid);
    return idx >= 0 ? &tasks[idx] : 0;
}

static volatile int selftest_a_done;
static volatile int selftest_b_done;

static void selftest_worker_a(void) {
    selftest_a_done++;
    task_sleep_ticks(task_now_ticks(), 1);
    selftest_a_done++;
}

static void selftest_worker_b(void) {
    selftest_b_done++;
    task_yield();
    selftest_b_done++;
}

int task_selftest(void) {
    selftest_a_done = 0;
    selftest_b_done = 0;
    int pa = task_spawn_kernel("st_a", selftest_worker_a);
    int pb = task_spawn_kernel("st_b", selftest_worker_b);
    if (pa <= 0 || pb <= 0) return -1;
    int ca = -1, cb = -1;
    if (task_wait(pa, &ca) != 0 || task_wait(pb, &cb) != 0) return -1;
    if (selftest_a_done != 2 || selftest_b_done != 2 || ca != 0 || cb != 0)
        return -1;
    task_log("task: selftest passed\n");
    return 0;
}
