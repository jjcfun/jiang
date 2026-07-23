#include <pthread.h>
#include <signal.h>
#include <unistd.h>

static pthread_t jiang_waiting_thread;

static void jiang_ignore_signal(int signal_number) {
    (void)signal_number;
}

void task_blocking_wait_install_interrupt(void) {
    struct sigaction action = {0};
    action.sa_handler = jiang_ignore_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR1, &action, NULL);
    jiang_waiting_thread = pthread_self();
}

long task_blocking_wait_interrupt_then_complete(void) {
    pthread_kill(jiang_waiting_thread, SIGUSR1);
    usleep(20000);
    return 19;
}
