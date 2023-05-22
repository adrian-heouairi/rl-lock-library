#include "panic.h"
#include "rl_lock_library.h"

int main() {
    rl_init_library();

#define FILENAME "/tmp/test-rl-fork.txt"
    rl_descriptor lfd = rl_open(FILENAME, O_CREAT | O_RDONLY | O_TRUNC, 0644);
    if (lfd.fd < 0 || lfd.file == NULL)
        PANIC_EXIT("rl_open()");

    printf("Open test file\n");

    struct flock lck;
    lck.l_type = F_RDLCK;
    lck.l_whence = SEEK_SET;
    lck.l_start = 0;
    lck.l_len = 10;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Placed read lock on [0; 10[\n");

    pid_t pid = rl_fork();
    if (pid < 0)
        PANIC_EXIT("rl_fork()");

    if (pid == 0) {
        printf("CHILD: Forked\n");

        if (rl_print_open_file(lfd.file, 0) < 0)
            PANIC_EXIT("rl_print_open_file()");

        lck.l_start = 5;
        lck.l_len = 10;

        if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
            PANIC_EXIT("rl_fcntl()");

        printf("CHILD: Placed read lock on [5; 15[\n");

        if (rl_print_open_file(lfd.file, 0) < 0)
            PANIC_EXIT("rl_print_open_file()");

        if (rl_close(lfd) < 0)
            PANIC_EXIT("rl_close()");

        printf("CHILD: Succesfully closed file description\n");
        
        if (rl_print_open_file(lfd.file, 0) < 0)
            PANIC_EXIT("rl_print_open_file()");
    } else
        printf("PARENT: Forked\n");

    return 0;
}
