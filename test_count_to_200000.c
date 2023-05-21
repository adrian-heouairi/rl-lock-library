#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

#include "rl_lock_library.h"

#define NAME "/tmp/test_count_to_2000.txt"
#define MAX 100000

void test() {

    /* Open file */
    pid_t pid = getpid();
    rl_descriptor lfd = rl_open(NAME, O_RDWR);
    if (lfd.fd == -1 || lfd.file == NULL) {
        printf("%ld: rl_open()\n", (long) pid);
        return;
    }
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

        if (code == -1) {
            printf("%ld: rl_fcntl()\n", (long)pid);
            return;
        }

        printf("%ld: got write lock\n", (long) pid);

        /* Read current value from file */
        if (lseek(lfd.fd, 0, SEEK_SET) < 0) {
            perror("lseek()");
            return;
        }
        int n = -1;
        if (read(lfd.fd, &n, sizeof(int)) < 0) {
            perror("read()");
            return;
        }

        printf("%ld: read %d\n", (long)pid, n);

        n++;

        /* Write incremented value to file */
        if (lseek(lfd.fd, 0, SEEK_SET) < 0) {
            perror("lseek()");
            return;
        }
        if (write(lfd.fd, &n, sizeof(int)) < 0) {
            perror("write()");
            return;
        }

        printf("%ld: wrote %d\n", (long)pid, n);

        /* Unlock region */
        lck.l_type = F_UNLCK;
        if (rl_fcntl(lfd, F_SETLK, &lck) == -1) {
            printf("%ld: rl_fcntl()\n", (long)pid);
            return;
        }

        printf("%ld: unlocked\n", (long)pid);
    }

    /* Close descriptor when finished */
    if (rl_close(lfd) == -1) {
        printf("%ld: rl_close()\n", (long)pid);
        return;
    }

    printf("Succesfully closed file\n");
}

int main() {
    printf("Test count to 20000\n");

    /* Initialize library */
    rl_init_library();

    /* Create new file and open it */
    rl_descriptor lfd = rl_open(NAME, O_CREAT | O_WRONLY | O_TRUNC,
            S_IRUSR | S_IWUSR);
    if (lfd.fd == -1 && lfd.file == NULL) {
        printf("rl_open()\n");
        return -1;
    }

    printf("Succesfully created file\n");

    /* Initialize counter */
    int count = 0;
    if (write(lfd.fd, &count, sizeof(int)) < 0) {
        perror("write()");
        return -1;
    }

    printf("Succesfully intialized counter\n");

    if (rl_close(lfd) == -1) {
        printf("rl_close()\n");
        return -1;
    }

    pid_t pid;
    if ((pid = fork()) == -1) {
        perror("fork()");
        return -1;
    }

    if (pid == 0) { /* child */
        test();
    } else { /* parent */
        test();
    }

    return 0;
}
