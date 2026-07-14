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

static void *run_delayed_add(void *opaque) {
    DelayedAddJob *job = opaque;
    *job->continuation->result = job->value + 1;
    job->continuation->resume(job->continuation->context);
    return 0;
}

void delayed_add(int64_t value, JiangAsyncContinuation *continuation) {
    static DelayedAddJob job;
    pthread_t thread;
    job.value = value;
    job.continuation = continuation;
    pthread_create(&thread, 0, run_delayed_add, &job);
    pthread_detach(thread);
}
