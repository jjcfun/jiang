#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sched.h>
#include <unistd.h>

_Static_assert(sizeof(pthread_mutex_t) <= 64, "pthread mutex storage");
_Static_assert(sizeof(pthread_cond_t) <= 64, "pthread condition storage");
_Static_assert(sizeof(pthread_attr_t) <= 64, "pthread attribute storage");
_Static_assert(_Alignof(pthread_mutex_t) <= _Alignof(uint64_t), "pthread mutex alignment");
_Static_assert(_Alignof(pthread_cond_t) <= _Alignof(uint64_t), "pthread condition alignment");
_Static_assert(_Alignof(pthread_attr_t) <= _Alignof(uint64_t), "pthread attribute alignment");
_Static_assert(sizeof(pthread_t) == sizeof(uintptr_t), "pthread handle width");
#ifdef __APPLE__
_Static_assert(sizeof(pthread_key_t) == sizeof(uintptr_t), "Darwin TLS key width");
#else
_Static_assert(sizeof(pthread_key_t) == sizeof(uint32_t), "Linux TLS key width");
#endif

static _Atomic int failed;
static _Atomic int serial_active;
static _Atomic int serial_count;
static _Atomic int concurrent_count;
static void *serial_identity;
static _Thread_local void *first_identity;
static _Atomic int first_count;
static _Atomic int second_count;
static _Atomic int nested_started;
static _Atomic int nested_leaves;
static _Atomic int nested_parents;

void set_serial_probe_identity(void *identity) { serial_identity = identity; }
void *serial_probe_identity(void) { return serial_identity; }

int64_t pthread_probe_worker_count(void) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count < 2 ? 2 : count;
}

void first_pool_probe_record(void *identity) {
    first_identity = identity;
    atomic_fetch_add(&first_count, 1);
    while (atomic_load(&first_count) != pthread_probe_worker_count()) sched_yield();
}

void second_pool_probe_record(void *identity) {
    // 第一阶段同时占住每个 worker，确保第二个队列只能复用这些线程的 TLS。
    if (!first_identity || first_identity == identity) atomic_store(&failed, 5);
    atomic_fetch_add(&second_count, 1);
}

void nested_pool_probe_enter(void) {
    atomic_fetch_add(&nested_started, 1);
    while (atomic_load(&nested_started) != pthread_probe_worker_count()) sched_yield();
}

void nested_pool_probe_done(int64_t leaf) {
    atomic_fetch_add(leaf ? &nested_leaves : &nested_parents, 1);
}

static size_t current_stack_size(void) {
#ifdef __APPLE__
    return pthread_get_stacksize_np(pthread_self());
#else
    pthread_attr_t attributes;
    size_t size = 0;
    if (pthread_getattr_np(pthread_self(), &attributes) != 0) return 0;
    pthread_attr_getstacksize(&attributes, &size);
    pthread_attr_destroy(&attributes);
    return size;
#endif
}

void record_pthread_probe(int64_t concurrent, int64_t expected_stack, int64_t ordinal) {
    size_t actual_stack = current_stack_size();
    // 系统可能把栈尾的页面余量计入返回值；配置仍必须控制实际栈的数量级。
    size_t padding = (size_t)sysconf(_SC_PAGESIZE);
    if (actual_stack < (size_t)expected_stack || actual_stack >= (size_t)expected_stack + padding) {
        fprintf(stderr, "pthread stack: expected=%lld actual=%zu\n", (long long)expected_stack, actual_stack);
        atomic_store(&failed, 1);
    }
    if (concurrent) {
        atomic_fetch_add(&concurrent_count, 1);
        return;
    }
    if (atomic_fetch_add(&serial_active, 1) != 0) atomic_store(&failed, 2);
    if (atomic_fetch_add(&serial_count, 1) != ordinal) atomic_store(&failed, 3);
    atomic_fetch_sub(&serial_active, 1);
}

int64_t pthread_probe_result(void) {
    if (atomic_load(&failed)) return atomic_load(&failed);
    int64_t workers = pthread_probe_worker_count();
    if (atomic_load(&first_count) != workers || atomic_load(&second_count) != workers
        || atomic_load(&nested_leaves) != workers || atomic_load(&nested_parents) != workers) return 6;
    return atomic_load(&serial_count) == 96 && atomic_load(&concurrent_count) == 96 ? 0 : 4;
}

#ifdef __APPLE__
// 截获主事件循环通知，确定性检查合并与重新通知；普通 worker 仍使用真实 pthread。
static void (*main_drain)(void *);
static void *main_context;
static int64_t main_notifications;
void dispatch_async_f(void *queue, void *context, void (*callback)(void *)) {
    (void)queue;
    main_drain = callback;
    main_context = context;
    ++main_notifications;
}
#endif
int64_t main_probe_notifications(void) {
#ifdef __APPLE__
    return main_notifications;
#else
    return -1;
#endif
}
void main_probe_flush(void) {
#ifdef __APPLE__
    if (!main_notifications) return;
    main_notifications = 0;
    main_drain(main_context);
#endif
}

// 非原子结果由 Group 完成/等待发布；四个外部 pthread 同时等待最后一项完成。
static int64_t group_values[96];
static _Atomic int group_waiters_ready;
struct group_wait_probe { void *group; void (*wait)(void *); };
void group_probe_write(int64_t index) { group_values[index] = index + 1; }
void group_probe_check(void) {
    for (int i = 0; i < 96; ++i) {
        if (group_values[i] != i + 1) atomic_store(&failed, 7);
    }
}
static void *group_wait_probe_main(void *raw) {
    struct group_wait_probe *probe = raw;
    atomic_fetch_add(&group_waiters_ready, 1);
    probe->wait(probe->group);
    group_probe_check();
    return NULL;
}
void group_probe_waiters(void *group, void *wait_fn, void *leave_fn) {
    pthread_t workers[4];
    struct group_wait_probe probe = {group, (void (*)(void *))wait_fn};
    atomic_store(&group_waiters_ready, 0);
    for (int i = 0; i < 4; ++i) {
        if (pthread_create(&workers[i], NULL, group_wait_probe_main, &probe)) __builtin_trap();
    }
    while (atomic_load(&group_waiters_ready) != 4) sched_yield();
    ((void (*)(void *))leave_fn)(group);
    for (int i = 0; i < 4; ++i) pthread_join(workers[i], NULL);
    for (int i = 0; i < 96; ++i) group_values[i] = 0;
}

// 先提交整个批次，再允许回调关闭自己的并发队列；已接受的任务必须全部收尾。
static _Atomic int close_probe_ready;
static _Atomic int close_probe_count;
int64_t concurrent_close_probe_enter(void) {
    while (!atomic_load(&close_probe_ready)) sched_yield();
    return atomic_fetch_add(&close_probe_count, 1);
}
void concurrent_close_probe_release(void) { atomic_store(&close_probe_ready, 1); }
int64_t concurrent_close_probe_result(void) { return atomic_load(&close_probe_count); }
