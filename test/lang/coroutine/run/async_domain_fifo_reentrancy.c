#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

typedef void (*JiangResume)(void *);

extern void __jiang_runtime_domain_enqueue(
    int64_t domain_id,
    int64_t domain_kind,
    void *context,
    void *token_slot,
    JiangResume resume,
    JiangResume callback
);

static _Atomic int64_t probe_state;
static pthread_mutex_t probe_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t probe_condition = PTHREAD_COND_INITIALIZER;

static void second_resume(void *context) {
    _Atomic int64_t *state = context;
    atomic_store(state, 2);
    pthread_mutex_lock(&probe_mutex);
    pthread_cond_signal(&probe_condition);
    pthread_mutex_unlock(&probe_mutex);
}

static void first_resume(void *context) {
    _Atomic int64_t *state = context;
    atomic_store(state, 1);
    __jiang_runtime_domain_enqueue(1, 0, context, 0, second_resume, 0);
    if (atomic_load(state) != 1) {
        atomic_store(state, 99);
    }
}

int64_t run_domain_fifo_probe(void) {
    atomic_store(&probe_state, 0);
    __jiang_runtime_domain_enqueue(1, 0, &probe_state, 0, first_resume, 0);
    pthread_mutex_lock(&probe_mutex);
    while (atomic_load(&probe_state) < 2) {
        pthread_cond_wait(&probe_condition, &probe_mutex);
    }
    pthread_mutex_unlock(&probe_mutex);
    return atomic_load(&probe_state);
}
