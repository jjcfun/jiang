#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

static _Atomic int64_t registration_ready;
static _Atomic int64_t registration_open;
static _Atomic int64_t cancel_started;
static pthread_t opener_thread;

void suspend_cancel_registration_block(void) {
    atomic_store(&registration_ready, 1);
    while (atomic_load(&registration_open) == 0) {
        sched_yield();
    }
}

void suspend_cancel_registration_wait_ready(void) {
    while (atomic_load(&registration_ready) == 0) {
        sched_yield();
    }
}

static void *open_registration(void *opaque) {
    (void)opaque;
    while (atomic_load(&cancel_started) == 0) {
        sched_yield();
    }
    usleep(10000);
    atomic_store(&registration_open, 1);
    return 0;
}

void suspend_cancel_registration_open_later(void) {
    pthread_create(&opener_thread, 0, open_registration, 0);
}

void suspend_cancel_registration_begin_cancel(void) {
    atomic_store(&cancel_started, 1);
}

void suspend_cancel_registration_join(void) {
    pthread_join(opener_thread, 0);
}
