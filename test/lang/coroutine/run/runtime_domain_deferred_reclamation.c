#include <dispatch/dispatch.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef void (*deferred_callback)(intptr_t);

struct deferred_work {
    intptr_t context;
    deferred_callback callback;
};

static _Thread_local int deferred_in_callback;
static _Atomic int64_t deferred_destroy_count;
static _Atomic int64_t deferred_destroyed_in_callback;

static void run_deferred_work(void *raw_work) {
    struct deferred_work *work = raw_work;
    deferred_in_callback = 1;
    work->callback(work->context);
    deferred_in_callback = 0;
    free(work);
}

void deferred_executor_submit(
    intptr_t context,
    deferred_callback callback
) {
    struct deferred_work *work = malloc(sizeof(*work));
    if (work == NULL) {
        abort();
    }
    work->context = context;
    work->callback = callback;
    dispatch_async_f(
        dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
        work,
        run_deferred_work
    );
}

void deferred_executor_record_destroy(void) {
    if (deferred_in_callback != 0) {
        atomic_store(&deferred_destroyed_in_callback, 1);
    }
    atomic_fetch_add(&deferred_destroy_count, 1);
}

void deferred_executor_wait_destroy(void) {
    for (int index = 0; index < 5000; ++index) {
        if (atomic_load(&deferred_destroy_count) != 0) {
            return;
        }
        usleep(1000);
    }
}

int64_t deferred_executor_destroy_count(void) {
    return atomic_load(&deferred_destroy_count);
}

int64_t deferred_executor_destroyed_in_callback(void) {
    return atomic_load(&deferred_destroyed_in_callback);
}
