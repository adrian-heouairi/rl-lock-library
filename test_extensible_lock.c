#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "panic.h"
#include "rl_lock_library.h"

/*
 * Creates a file where the process places 3 uncontiguous read locks, then it
 * unlocks the entirety of the file with an extensible lock, places an
 * extensible write lock at the 10th byte and then divides it in two parts by
 * putting a read lock on [15; 20[. The result should be a finite write lock on
 * [10; 15[, a finite read lock on [15; 20[ and an extensible write lock at the
 * 20th byte. Finally, it places an extensible read lock at the 12th byte, which
 * leaves a write lock on [10; 12[ and an extensible read lock at the 12th byte.
 * After that, it creates a child process, To imitate another process without
 * family linkage with its parent, the child process starts by closing the
 * locked file descriptor inherited by its father and tries to put an extensible
 * write lock at the 11th byte, which is impossible because the parent process
 * owns a write lock on the bytes [10; 12[. It then tries to place a read lock
 * on [25; 50[, which is succesfully placed as no incompatible locks overlap on
 * the segment. The child closes its descriptors then quits, the parent process
 * in the meantime waits for the death of its child, then closes its descriptors
 * and quits.
 */

int main() {
#define FILENAME "/tmp/test-extensible-lock.txt"
    rl_init_library();

    rl_descriptor lfd = rl_open(FILENAME, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (lfd.fd < 0 || lfd.file == NULL)
        PANIC_EXIT("rl_open()");

    printf("Opened file\n");

    struct flock lck;
    lck.l_type = F_RDLCK;
    lck.l_whence = SEEK_SET;
    lck.l_start = 1;
    lck.l_len = 3;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Placed read lock on [1; 4[\n");

    lck.l_start = 5;
    lck.l_len = 3;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Placed read lock on [5; 8[\n");

    lck.l_start = 9;
    lck.l_len = 3;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Placed read lock on [9; 12[\n");

    if (rl_print_open_file_safe(lfd.file, 0) < 0)
        PANIC_EXIT("rl_print_open_file()");
    printf("\n");

    lck.l_type = F_UNLCK;
    lck.l_start = 0;
    lck.l_len = 0;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Unlocked entire file\n");

    if (rl_print_open_file_safe(lfd.file, 0) < 0)
        PANIC_EXIT("rl_print_open_file()");
    printf("\n");

    lck.l_type = F_WRLCK;
    lck.l_start = 10;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Placed extensible write lock at 10\n");

    if (rl_print_open_file_safe(lfd.file, 0) < 0)
        PANIC_EXIT("rl_print_open_file()");
    printf("\n");

    lck.l_type = F_RDLCK;
    lck.l_start = 15;
    lck.l_len = 5;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Placed read lock on [15; 20[\n");

    if (rl_print_open_file_safe(lfd.file, 0) < 0)
        PANIC_EXIT("rl_print_open_file()");
    printf("\n");

    lck.l_start = 12;
    lck.l_len = 0;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Placed extensible read lock 12\n");

    if (rl_print_open_file_safe(lfd.file, 0) < 0)
        PANIC_EXIT("rl_print_open_file()");
    printf("\n");

    pid_t pid = rl_fork();
    if (pid == 1)
        PANIC_EXIT("fork()");

    if (pid == 0) { /* child */
        printf("Created child process\n\n");
        
        if (rl_close(lfd) < 0)
            PANIC_EXIT("rl_close()");

        printf("Child closes descriptors of parent process\n");

        rl_descriptor lfd2 = rl_open(FILENAME, O_RDWR);
        if (lfd2.fd < 0 || lfd2.file == NULL)
            PANIC_EXIT("rl_open()");

        printf("Child opens file\n");
        
        struct flock lck2;
        lck2.l_type = F_WRLCK;
        lck.l_whence = SEEK_SET;
        lck.l_start = 11;
        lck.l_len = 0;

        if (rl_fcntl(lfd2, F_SETLK, &lck2) == 0) {
            PANIC_EXIT("rl_fcntl()");
        } else 
            printf("Attempt to place ext. write lock at 11th byte failed\n");

        if (rl_print_open_file_safe(lfd.file, 0) < 0)
            PANIC_EXIT("rl_print_open_file()");
        printf("\n");
        
        lck2.l_type = F_RDLCK;
        lck2.l_whence = SEEK_SET;
        lck2.l_start = 25;
        lck2.l_len = 25;

        if (rl_fcntl(lfd2, F_SETLK, &lck2) < 0)
            PANIC_EXIT("rl_fcntl()");

        printf("Child placed read lock on [25; 50[\n");

        if (rl_print_open_file_safe(lfd.file, 0) < 0)
            PANIC_EXIT("rl_print_open_file()");
        printf("\n");

        if (rl_close(lfd) < 0)
            PANIC_EXIT("rl_close()");

        printf("Closed locked file description in child process\n");
    } else { /* parent */
        if (wait(NULL) < 0)
            PANIC_EXIT("wait()");

        if (rl_close(lfd) < 0)
            PANIC_EXIT("rl_close()");

        printf("Closed locked file description in parent process\n");

        if (unlink(FILENAME) < 0)
            PANIC_EXIT("unlink()");
    }

    return 0;
}
