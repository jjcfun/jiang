#include <stdint.h>

typedef void (*JiangResume)(void *);

typedef struct {
    int64_t *result;
    void *context;
    JiangResume resume;
} JiangAsyncContinuation;

void external_raw_add(int64_t value, JiangAsyncContinuation *continuation) {
    *continuation->result = value + 6;
    continuation->resume(continuation->context);
}
