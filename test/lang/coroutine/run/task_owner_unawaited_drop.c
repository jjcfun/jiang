#include <stdatomic.h>
#include <stdint.h>
#include <sched.h>

static _Atomic int64_t drop_count = 0;

void task_owner_unawaited_record_drop(void) {
    atomic_fetch_add_explicit(&drop_count, 1, memory_order_relaxed);
}

int64_t task_owner_unawaited_drop_count(void) {
    return atomic_load_explicit(&drop_count, memory_order_relaxed);
}

void task_owner_unawaited_wait_for_drops(int64_t expected) {
    while (atomic_load_explicit(&drop_count, memory_order_acquire) < expected) {
        sched_yield();
    }
}
