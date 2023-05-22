#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

#include "panic.h"
#include "rl_lock_library.h"

#define NAME "/tmp/test_count_to_50000.txt"
#define MAX 25000

/*
 * Creates a file where to store an integer, then creates a child process.
 * Each process tries to take lock until it gets it, reads the value, increments
 * it and writes it back before releasing the lock, MAX times.
 * When the two process ends, the value must be equal to 2 * MAX.
 */

void count_to_max() {

    /* Open file */
    pid_t pid = getpid();
    rl_descriptor lfd = rl_open(NAME, O_RDWR);
    if (lfd.fd == -1 || lfd.file == NULL)
        PANIC_EXIT("rl_open()");

    printf("%ld: open %s\n", (long) pid, NAME);

    /* Increment value 100k times */
    for (int i = 0; i < MAX; i++) {
        struct flock lck;
        lck.l_start = 0;
        lck.l_len = sizeof(int);
        lck.l_whence = SEEK_SET;
        lck.l_type = F_WRLCK;
        lck.l_pid = pid;
        
        /* Wait for write lock */
        int code;
        while ((code = rl_fcntl(lfd, F_SETLK, &lck)) == -1 && errno == EAGAIN)
            printf("%ld: incompatible lock\n", (long)pid);

        if (code == -1)
            PANIC_EXIT("rl_fcntl()");

        printf("%ld: got write lock\n", (long) pid);

        /* Read current value from file */
        if (lseek(lfd.fd, 0, SEEK_SET) < 0)
            PANIC_EXIT("lseek()");

        int n = -1;
        if (read(lfd.fd, &n, sizeof(int)) < 0)
            PANIC_EXIT("read()");

        printf("%ld: read %d\n", (long)pid, n);

        n++;

        /* Write incremented value to file */
        if (lseek(lfd.fd, 0, SEEK_SET) < 0)
            PANIC_EXIT("lseek()");

        if (write(lfd.fd, &n, sizeof(int)) < 0)
            PANIC_EXIT("write()");

        printf("%ld: wrote %d\n", (long)pid, n);

        /* Unlock region */
        lck.l_type = F_UNLCK;
        if (rl_fcntl(lfd, F_SETLK, &lck) == -1)
            PANIC_EXIT("rl_fcntl()");

        printf("%ld: unlocked\n", (long)pid);
    }

    /* Close descriptor when finished */
    if (rl_close(lfd) == -1)
        PANIC_EXIT("rl_close()");

    printf("Succesfully closed file\n");
}

int main() {
    rl_init_library();

    /* Create new file and open it */
    rl_descriptor lfd = rl_open(NAME, O_CREAT | O_WRONLY | O_TRUNC,
            S_IRUSR | S_IWUSR);
    if (lfd.fd == -1 && lfd.file == NULL)
        PANIC_EXIT("rl_open()");

    printf("Succesfully created file\n");

    /* Initialize counter */
    int count = 0;
    if (write(lfd.fd, &count, sizeof(int)) < 0)
        PANIC_EXIT("write()");

    printf("Succesfully intialized counter\n");

    if (rl_close(lfd) == -1)
        PANIC_EXIT("rl_close()");

    pid_t pid;
    if ((pid = fork()) == -1)
        PANIC_EXIT("fork()");

    count_to_max();

    return 0;
}
