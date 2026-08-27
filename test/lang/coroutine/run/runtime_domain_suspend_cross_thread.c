#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

typedef void (*resume_callback)(intptr_t);

static _Atomic int64_t enqueue_count;
static _Atomic int64_t destroy_count;
static intptr_t resume_context;
static resume_callback resume_function;
static pthread_t resume_thread;

static void *resume_from_worker(void *opaque) {
    (void)opaque;
    usleep(1000);
    resume_function(resume_context);
    return 0;
}

void runtime_cross_thread_submit(
    intptr_t context,
    resume_callback callback
) {
    resume_context = context;
    resume_function = callback;
    pthread_create(&resume_thread, 0, resume_from_worker, 0);
}

void runtime_cross_thread_join(void) {
    pthread_join(resume_thread, 0);
}

void runtime_cross_thread_record_enqueue(void) {
    atomic_fetch_add_explicit(&enqueue_count, 1, memory_order_relaxed);
}

void runtime_cross_thread_record_destroy(void) {
    atomic_fetch_add_explicit(&destroy_count, 1, memory_order_relaxed);
}

int64_t runtime_cross_thread_enqueue_count(void) {
    return atomic_load_explicit(&enqueue_count, memory_order_relaxed);
}

int64_t runtime_cross_thread_destroy_count(void) {
    return atomic_load_explicit(&destroy_count, memory_order_relaxed);
}
