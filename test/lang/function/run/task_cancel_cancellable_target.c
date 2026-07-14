#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

typedef void (*JiangResume)(void *);

typedef struct {
    int64_t *result;
    void *context;
    JiangResume resume;
} JiangAsyncContinuation;

static _Atomic int64_t target_ready;
static _Atomic int64_t target_cancelled;
static _Atomic int64_t target_cancel_count;
static _Atomic int64_t worker_blocked;
static _Atomic int64_t worker_release;
static _Atomic int64_t cancel_probe_started;
static _Atomic int64_t wrong_domain;
static pthread_t target_thread;
static pthread_t worker_release_thread;
static JiangAsyncContinuation *target_continuation;

static void *run_cancellable_target(void *opaque) {
    (void)opaque;
    atomic_store(&target_ready, 1);
    while (atomic_load(&target_cancelled) == 0) {
        sched_yield();
    }
    *target_continuation->result = 7;
    target_continuation->resume(target_continuation->context);
    return 0;
}

void cancellable_target_wait(JiangAsyncContinuation *continuation) {
    target_continuation = continuation;
    pthread_create(&target_thread, 0, run_cancellable_target, 0);
}

void cancellable_target_ready(void) {
    while (atomic_load(&target_ready) == 0) {
        sched_yield();
    }
}

void cancellable_target_cancel(void) {
    if (atomic_load(&worker_blocked) != 0) {
        atomic_store(&wrong_domain, 1);
    }
    atomic_fetch_add(&target_cancel_count, 1);
    atomic_store(&target_cancelled, 1);
}

void cancellable_target_join(void) {
    pthread_join(target_thread, 0);
}

int64_t cancellable_target_cancel_count(void) {
    return atomic_load(&target_cancel_count);
}

void cancellable_target_block_worker(void) {
    atomic_store(&worker_blocked, 1);
    while (atomic_load(&worker_release) == 0) {
        sched_yield();
    }
    atomic_store(&worker_blocked, 0);
}

void cancellable_target_wait_worker_blocked(void) {
    while (atomic_load(&worker_blocked) == 0) {
        sched_yield();
    }
}

static void *release_cancellable_worker(void *opaque) {
    (void)opaque;
    while (atomic_load(&cancel_probe_started) == 0) {
        sched_yield();
    }
    struct timespec delay = { .tv_sec = 0, .tv_nsec = 50000000 };
    nanosleep(&delay, 0);
    atomic_store(&worker_release, 1);
    return 0;
}

void cancellable_target_schedule_worker_release(void) {
    pthread_create(&worker_release_thread, 0, release_cancellable_worker, 0);
}

void cancellable_target_begin_cancel_probe(void) {
    atomic_store(&cancel_probe_started, 1);
}

void cancellable_target_join_worker_release(void) {
    pthread_join(worker_release_thread, 0);
}

int64_t cancellable_target_wrong_domain(void) {
    return atomic_load(&wrong_domain);
}
