/*
 * Adrian HEOUAIRI
 * Guillermo MORON USON
 */

#include "rl_lock_library.h"
#define RL_MAX_OWNERS 32
#define RL_MAX_LOCKS 32
#define RL_MAX_FILES 256
#define RL_FREE_OWNER -1
#define RL_FREE_FILE NULL
#define RL_NO_NEXT_LOCK -1
#define RL_FREE_LOCK -2
#define RL_NO_LOCKS -2

struct rl_owner {
    pid_t pid;
    int fd;
};

struct rl_lock {
    int next_lock;
    off_t starting_offset;
    off_t len;
    short type; /* F_RDLCK, F_WRLCK */
    size_t nb_owners;
    rl_owner lock_owners[RL_MAX_OWNERS];
};

struct rl_open_file {
    int first;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    rl_lock lock_table[RL_MAX_LOCKS];
};

struct rl_descriptor {
    int fd;
    rl_open_file *of;
};

struct rl_all_files {
    int nb_files;
    rl_open_file *open_files[RL_MAX_FILES];
};

/******************************************************************************/

/*
 * Initializes pmutex for process synchronization.
 */
static int initialize_mutex(pthread_mutex_t *pmutex) {
    pthread_mutexattr_t mutexattr;
    int code;

    code = pthread_mutexattr_init(&mutexattr);
    if (code != 0)
        return code;
    code = pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
    if(code != 0)
        return code;
    return pthread_mutex_init(pmutex, &mutexattr);
}

/*
 * Initializes pcond for process synchronization.
 */
static int initialize_cond(pthread_cond_t *pcond) {
    pthread_condattr_t condattr;
    int code;

    code = pthread_condattr_init(&condattr);
    if(code != 0)
        return code;
    code = pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
    if (code != 0)
        return code;
    return pthread_cond_init(pcond, &condattr);
}

/******************************************************************************/

/*
 * Returns 1 if owner is free, 0 otherwise. An owner is free if owner->fd equals
 * RL_FREE_OWNER.
 */
static int is_owner_free(rl_owner *owner) {
    return owner->fd == RL_FREE_OWNER;
}

/*
 * Erases the given owner. If owner is NULL, does nothing.
 */
static void erase_owner(rl_owner *owner) {
    if (owner != NULL) {
        owner->pid = (pid_t) RL_FREE_OWNER;
        owner->fd = RL_FREE_OWNER;
    }
}

/******************************************************************************/

/*
 * If lock is not NULL, moves the owners in lock->lock_owners in order to fit in
 * the lock->nb_owners first cells. If lock->nb_owners is strictly inferior to
 * 0 or strictly superior to RL_MAX_OWNERS, or lock is NULL, returns -1.
 * If lock->nb_owners is superior to the real number of owners, returns -1. If
 * lock->nb_owners is less then the real number of owners (but superior to 0),
 * only the first lock->nb_owners owners will move in order to fit the first
 * cells, leaving the rest unchanged. On success and in the latter case, returns
 * 0.
 */
static int organize_owners(rl_lock *lock) {
    int i, j;

    if (lock == NULL || lock->nb_owners < 0 || lock->nb_owners > RL_MAX_OWNERS)
        return -1;
    for (i = 0; i < lock->nb_owners; i++) {
        if (is_owner_free(&lock->lock_owners[i])) {
            j = i + 1;
            while (j < RL_MAX_OWNERS && is_owner_free(&lock->lock_owners[j]))
                j++;
            if (j >= RL_MAX_OWNERS)
                return -1;
            lock->lock_owners[i] = lock->lock_owners[j];
            erase_owner(&lock->lock_owners[j]);
        }
    }
    return 0;
}

/*
 * Returns 1 if o1.pid == o2.pid && o1.fd == o2.fd, 0 otherwise.
 */
static int equals(rl_owner o1, rl_owner o2) {
    return o1.pid == o2.pid && o1.fd == o2.fd;
}

/******************************************************************************/

/*
 * Returns 1 if lock is the last lock, that is if lock is not NULL and
 * lock->next_lock == RL_NO_NEXT_LOCK.
 */
static int is_last(rl_lock *lock) {
    return lock != NULL && lock->next_lock == RL_NO_NEXT_LOCK;
}

/*
 * rl_close() closes lfd.fd with close() and deletes all the locks associated to
 * that file descriptor and the calling process by erasing the corresponding
 * owner from the lock owners table of each concerned lock. If the corresponding
 * owner is the last remaining owner of a lock, the lock is also removed from
 * the lock table of the open file. If the call to close() on lfd.fd fails,
 * returns -1, thus leaving the lock table unmodified. If 
 */
int rl_close(rl_descriptor lfd) {
    rl_owner lfd_owner;
    rl_open_file *of;
    rl_lock *cur, *prev;
    int i, lock_owners_count, err;

    if (close(lfd.fd) == -1) {
        perror("close()");
        return -1;
    }

    lfd_owner = (rl_owner) {.pid = getpid(), .fd = lfd.fd};
    of = lfd.of;
    err = pthread_mutex_lock(&of->mutex);
    if (err != 0) {
        fprintf(stderr, "%s\n", strerror(err));
        return -1;
    }
    cur = &of->lock_table[of->first];
    prev = cur;

    while (1) {
        /* count owners after erase of lfd_owner */
        lock_owners_count = 0;
        for (i = 0; i < cur->nb_owners; i++) {
            if (equals(cur->lock_owners[i], lfd_owner)) {
                erase_owner(&cur->lock_owners[i]);
            } else {
                lock_owners_count++;
            }
        }
        
        /* organize owners to fit in nb_owners first cells */
        if (lock_owners_count != 0) {
            cur->nb_owners = lock_owners_count;
            organize_owners(cur);
        }

        if (is_last(cur)) {
            /* erase lock if lfd_owner was the last */
            if (lock_owners_count == 0) {
                prev->next_lock = RL_NO_NEXT_LOCK;
                cur->next_lock = RL_FREE_LOCK;
            }
            break;
        } else {
            if (lock_owners_count == 0) {
                /* cur is first and has next, next becomes new first */
                if (cur == &of->lock_table[of->first]){
                    of->first = cur->next_lock;
                    cur = &of->lock_table[of->first];
                    prev->next_lock = RL_FREE_LOCK;
                    prev = cur;
                } else { /* cur is neither first nor last */
                    prev->next_lock = cur->next_lock;
                    cur->next_lock = RL_FREE_LOCK;
                    cur = &of->lock_table[prev->next_lock];
                }
            } else {
                prev = cur;
                cur = &of->lock_table[prev->next_lock];
            }
        }
    }

    err = pthread_mutex_unlock(&of->mutex);
    if (err != 0) {
        fprintf(stderr, "%s\n", strerror(err));
        return -1;
    }
    return 0;
}
