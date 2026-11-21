#ifndef FLAIROS_TASK_H
#define FLAIROS_TASK_H

#include <stdint.h>

#define TASK_MAX        16
#define TASK_NAME_MAX   24
#define TASK_KSTACK_SZ  4096
#define TASK_QUANTUM    2

enum task_state {
    TASK_UNUSED = 0,
    TASK_READY = 1,
    TASK_RUNNING = 2,
    TASK_SLEEPING = 3,
    TASK_BLOCKED = 4,
    TASK_ZOMBIE = 5
};

struct task {
    int pid;
    int ppid;
    enum task_state state;
    uint32_t esp;
    uintptr_t kstack_base;
    uint32_t cr3;
    uint32_t wake_tick;
    uint32_t slices;
    int exit_code;
    char name[TASK_NAME_MAX];
};

void task_init(void);
int task_spawn_kernel(const char *name, void (*entry)(void));
void task_yield(void);
void task_sleep_ticks(uint32_t now_ticks, uint32_t n);
void task_exit(int code);
int task_wait(int pid, int *exit_code);
int task_kill(int pid);
void task_tick(uint32_t now_ticks);
int task_current_pid(void);
int task_active_count(void);
const struct task *task_get(int pid);
int task_selftest(void);

#endif
