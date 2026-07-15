#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <pthread.h>

typedef void (*JiangResume)(void *);

typedef struct {
    int64_t *result;
    void *context;
    JiangResume resume;
} JiangAsyncContinuation;

static _Atomic(JiangAsyncContinuation *) same_continuation;
static _Atomic(JiangAsyncContinuation *) cross_continuation;
static _Atomic uintptr_t same_signal_thread;
static _Atomic uintptr_t cross_signal_thread;
static _Atomic int64_t same_signal_active;
static _Atomic int64_t cross_signal_active;
static _Atomic int64_t same_resumed_inline;
static _Atomic int64_t cross_resumed_inline;

void wait_same_signal(JiangAsyncContinuation *continuation) {
    atomic_store_explicit(&same_continuation, continuation, memory_order_release);
}

int64_t signal_same_and_observe(void) {
    JiangAsyncContinuation *continuation = atomic_load_explicit(
        &same_continuation, memory_order_acquire
    );
    atomic_store_explicit(
        &same_signal_thread, (uintptr_t)pthread_self(), memory_order_release
    );
    atomic_store_explicit(&same_signal_active, 1, memory_order_release);
    *continuation->result = 1;
    continuation->resume(continuation->context);
    int64_t observed = atomic_load_explicit(&same_resumed_inline, memory_order_acquire);
    atomic_store_explicit(&same_signal_active, 0, memory_order_release);
    return observed;
}

void mark_same_resumed(void) {
    uintptr_t signal_thread = atomic_load_explicit(&same_signal_thread, memory_order_acquire);
    int64_t active = atomic_load_explicit(&same_signal_active, memory_order_acquire);
    if (active != 0 && signal_thread == (uintptr_t)pthread_self()) {
        atomic_store_explicit(&same_resumed_inline, 1, memory_order_release);
    }
}

void wait_cross_signal(JiangAsyncContinuation *continuation) {
    atomic_store_explicit(&cross_continuation, continuation, memory_order_release);
}

int64_t signal_cross_and_observe(void) {
    JiangAsyncContinuation *continuation = 0;
    while (continuation == 0) {
        continuation = atomic_load_explicit(&cross_continuation, memory_order_acquire);
        if (continuation == 0) {
            sched_yield();
        }
    }
    atomic_store_explicit(
        &cross_signal_thread, (uintptr_t)pthread_self(), memory_order_release
    );
    atomic_store_explicit(&cross_signal_active, 1, memory_order_release);
    *continuation->result = 1;
    continuation->resume(continuation->context);
    int64_t observed = atomic_load_explicit(&cross_resumed_inline, memory_order_acquire);
    atomic_store_explicit(&cross_signal_active, 0, memory_order_release);
    return observed;
}

void mark_cross_resumed(void) {
    uintptr_t signal_thread = atomic_load_explicit(&cross_signal_thread, memory_order_acquire);
    int64_t active = atomic_load_explicit(&cross_signal_active, memory_order_acquire);
    if (active != 0 && signal_thread == (uintptr_t)pthread_self()) {
        atomic_store_explicit(&cross_resumed_inline, 1, memory_order_release);
    }
}
