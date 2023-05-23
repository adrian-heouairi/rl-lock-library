#include <sys/types.h>
#include <sys/wait.h>

#include "panic.h"
#include "rl_lock_library.h"

/*
 * The parent process creates a new empty file. It places a read lock on the
 * first 10 bytes of the file, forks then waits for the death of its child.
 * After the rl_fork, the child is added as owner of every lock owned by the
 * parent. The child process places a read lock on the bytes [5; 15[, which
 * causes the removal of its read lock on [0; 10[ co-owned with the parent and
 * the application of a new read lock, atomically, on the bytes [0; 15[, of
 * which it is the unique owner. This test demonstrates the action of rl_fork
 * and the possibility to have several read locks on the same region. Also on
 * rl_close, the locks placed by each process are removed.
 */

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

        if (rl_print_open_file_safe(lfd.file, 1) < 0)
            PANIC_EXIT("rl_print_open_file_safe()");
        printf("\n");

        lck.l_start = 5;
        lck.l_len = 10;

        if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
            PANIC_EXIT("rl_fcntl()");

        printf("CHILD: Placed read lock on [5; 15[\n");

        if (rl_print_open_file_safe(lfd.file, 1) < 0)
            PANIC_EXIT("rl_print_open_file_safe()");
        printf("\n");

        if (rl_close(lfd) < 0)
            PANIC_EXIT("rl_close()");
        printf("\n");
        printf("CHILD: Succesfully closed file description\n");

        return 0;
    } else {
        printf("PARENT: Forked\n");

        printf("PARENT: Waiting for child to die\n");
        if (wait(NULL) < 0)
            PANIC_EXIT("wait()");

        if (rl_close(lfd) < 0)
            PANIC_EXIT("rl_close()");

        printf("PARENT: Succesfully closed file description\n");
    }

    return 0;
}
