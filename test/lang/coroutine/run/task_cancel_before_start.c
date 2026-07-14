#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

static _Atomic int64_t gate_ready;
static _Atomic int64_t gate_open;

void cancel_gate_block(void) {
    atomic_store(&gate_ready, 1);
    while (atomic_load(&gate_open) == 0) {
        sched_yield();
    }
}

void cancel_gate_wait_ready(void) {
    while (atomic_load(&gate_ready) == 0) {
        sched_yield();
    }
}

void cancel_gate_open_later(void) {
    usleep(10000);
    atomic_store(&gate_open, 1);
}
