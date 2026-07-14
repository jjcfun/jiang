#include <pthread.h>
#include <stdint.h>

typedef void (*JiangResume)(void *);

typedef struct {
    int64_t *result;
    void *context;
    JiangResume resume;
} JiangAsyncContinuation;

typedef struct {
    int64_t value;
    JiangAsyncContinuation *continuation;
} DelayedAddJob;

static pthread_t pending_thread;
static DelayedAddJob pending_job;

static void *run_delayed_add(void *opaque) {
    DelayedAddJob *job = opaque;
    *job->continuation->result = job->value + 1;
    job->continuation->resume(job->continuation->context);
    return 0;
}

void delayed_add(int64_t value, JiangAsyncContinuation *continuation) {
    pending_job.value = value;
    pending_job.continuation = continuation;
    pthread_create(&pending_thread, 0, run_delayed_add, &pending_job);
}

void wait_all(void) {
    pthread_join(pending_thread, 0);
}
