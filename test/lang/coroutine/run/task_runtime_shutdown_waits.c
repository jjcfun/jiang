#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

static atomic_int jiang_shutdown_task_completed;

static void jiang_verify_shutdown_task(void) {
    if (atomic_load_explicit(&jiang_shutdown_task_completed, memory_order_acquire) == 0) {
        _Exit(91);
    }
}

void task_runtime_shutdown_prepare(void) {
    atomic_store_explicit(&jiang_shutdown_task_completed, 0, memory_order_relaxed);
    atexit(jiang_verify_shutdown_task);
}

void task_runtime_shutdown_complete(void) {
    usleep(20000);
    atomic_store_explicit(&jiang_shutdown_task_completed, 1, memory_order_release);
}
