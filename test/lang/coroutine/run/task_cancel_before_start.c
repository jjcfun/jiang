#include <sched.h>
#include <pthread.h>
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

static void *cancel_gate_open_thread(void *unused) {
    usleep(10000);
    atomic_store(&gate_open, 1);
    return unused;
}

void cancel_gate_open_later(void) {
    pthread_t thread;
    if (pthread_create(&thread, 0, cancel_gate_open_thread, 0) == 0) {
        pthread_detach(thread);
    }
}
