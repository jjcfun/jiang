#include <dispatch/dispatch.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef void (*delayed_callback)(intptr_t);

struct delayed_work {
    intptr_t context;
    delayed_callback callback;
};

static _Thread_local int delayed_in_callback;
static _Atomic int64_t delayed_callback_count;
static _Atomic int64_t delayed_destroy_count;
static _Atomic int64_t delayed_destroyed_in_callback;

static void run_delayed_work(void *raw_work) {
    struct delayed_work *work = raw_work;
    delayed_in_callback = 1;
    atomic_fetch_add(&delayed_callback_count, 1);
    work->callback(work->context);
    delayed_in_callback = 0;
    free(work);
}

void delayed_executor_submit(
    intptr_t context,
    delayed_callback callback
) {
    struct delayed_work *work = malloc(sizeof(*work));
    if (work == NULL) {
        abort();
    }
    work->context = context;
    work->callback = callback;
    dispatch_after_f(
        dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC),
        dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
        work,
        run_delayed_work
    );
}

void delayed_executor_record_destroy(void) {
    if (delayed_in_callback != 0) {
        atomic_store(&delayed_destroyed_in_callback, 1);
    }
    atomic_fetch_add(&delayed_destroy_count, 1);
}

void delayed_executor_wait_destroy(void) {
    for (int index = 0; index < 5000; ++index) {
        if (atomic_load(&delayed_destroy_count) != 0) {
            return;
        }
        usleep(1000);
    }
}

int64_t delayed_executor_callback_count(void) {
    return atomic_load(&delayed_callback_count);
}

int64_t delayed_executor_destroy_count(void) {
    return atomic_load(&delayed_destroy_count);
}

int64_t delayed_executor_destroyed_in_callback(void) {
    return atomic_load(&delayed_destroyed_in_callback);
}
