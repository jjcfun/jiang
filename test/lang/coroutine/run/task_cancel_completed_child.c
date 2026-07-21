#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <unistd.h>

static _Atomic int child_ready;
static _Atomic int child_open;

void completed_child_block(void) {
    atomic_store(&child_ready, 1);
    while (atomic_load(&child_open) == 0) {
        sched_yield();
    }
}

void completed_child_wait_ready(void) {
    while (atomic_load(&child_ready) == 0) {
        sched_yield();
    }
}

static void *open_completed_child(void *opaque) {
    (void)opaque;
    usleep(10000);
    atomic_store(&child_open, 1);
    return 0;
}

void completed_child_open_later(void) {
    pthread_t thread;
    if (pthread_create(&thread, 0, open_completed_child, 0) == 0) {
        pthread_detach(thread);
    }
}
