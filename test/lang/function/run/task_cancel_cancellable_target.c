#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>

typedef void (*JiangResume)(void *);

typedef struct {
    int64_t *result;
    void *context;
    JiangResume resume;
} JiangAsyncContinuation;

static _Atomic int64_t target_ready;
static _Atomic int64_t target_cancelled;
static _Atomic int64_t target_cancel_count;
static pthread_t target_thread;
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
    atomic_fetch_add(&target_cancel_count, 1);
    atomic_store(&target_cancelled, 1);
}

void cancellable_target_join(void) {
    pthread_join(target_thread, 0);
}

int64_t cancellable_target_cancel_count(void) {
    return atomic_load(&target_cancel_count);
}
