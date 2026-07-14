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
} PendingValue;

static PendingValue pending[2];
static int64_t pending_count;

void delayed_value(int64_t value, JiangAsyncContinuation *continuation) {
    if (pending_count >= 2) {
        return;
    }
    pending[pending_count].value = value;
    pending[pending_count].continuation = continuation;
    pending_count += 1;
}

static void *complete_pending(void *unused) {
    (void)unused;
    while (pending_count > 0) {
        pending_count -= 1;
        PendingValue *value = &pending[pending_count];
        *value->continuation->result = value->value;
        value->continuation->resume(value->continuation->context);
    }
    return 0;
}

int64_t complete_all(void) {
    int64_t started = pending_count;
    pthread_t thread;
    pthread_create(&thread, 0, complete_pending, 0);
    pthread_detach(thread);
    return started;
}
