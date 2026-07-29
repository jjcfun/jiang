#include <dispatch/dispatch.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

typedef void (*shutdown_callback)(intptr_t);

static atomic_int jiang_shutdown_executor_completed;

static void verify_shutdown_executor(void) {
    if (atomic_load_explicit(
        &jiang_shutdown_executor_completed,
        memory_order_acquire
    ) == 0) {
        _Exit(91);
    }
}

void shutdown_executor_submit(
    intptr_t context,
    shutdown_callback callback
) {
    dispatch_after_f(
        dispatch_time(DISPATCH_TIME_NOW, 20000000),
        dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
        (void *)context,
        (dispatch_function_t)callback
    );
}

void shutdown_executor_prepare(void) {
    atomic_store_explicit(
        &jiang_shutdown_executor_completed,
        0,
        memory_order_relaxed
    );
    atexit(verify_shutdown_executor);
}

void shutdown_executor_complete(void) {
    atomic_store_explicit(
        &jiang_shutdown_executor_completed,
        1,
        memory_order_release
    );
}
