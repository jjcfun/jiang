#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static long long monotonic_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        perror("clock_gettime");
        exit(2);
    }
    return (long long)value.tv_sec * 1000000000LL + value.tv_nsec;
}

static long long peak_rss_kb(struct rusage *usage) {
#if defined(__APPLE__)
    return (long long)usage->ru_maxrss / 1024LL;
#else
    return (long long)usage->ru_maxrss;
#endif
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s label command [args...]\n", argv[0]);
        return 2;
    }
    long long start = monotonic_ns();
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return 2;
    }
    if (child == 0) {
        execvp(argv[2], &argv[2]);
        fprintf(stderr, "execvp %s: %s\n", argv[2], strerror(errno));
        _exit(127);
    }
    int status = 0;
    struct rusage usage = {0};
    if (wait4(child, &status, 0, &usage) < 0) {
        perror("wait4");
        return 2;
    }
    long long elapsed_ns = monotonic_ns() - start;
    printf("case=%s elapsed_ms=%.3f peak_rss_kb=%lld\n",
           argv[1], (double)elapsed_ns / 1000000.0, peak_rss_kb(&usage));
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 128 + WTERMSIG(status);
}
