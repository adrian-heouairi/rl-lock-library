/*
 * Adrian HEOUAIRI
 * Guillermo MORON USON
 */

#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200112L

#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>

#include "rl_lock_library.h"

#define RL_MAX_OWNERS 32
#define RL_MAX_LOCKS 32
#define RL_MAX_FILES 256
#define RL_FREE_OWNER -1
#define RL_FREE_FILE NULL
#define RL_FREE_LOCK -2
#define SHM_PREFIX "f"

/**
 * @brief All the file descriptions opened by this process
 */
static rl_all_files rla;

/******************************************************************************/

/**
 * @brief The owner of a locked segment
 */
struct rl_owner {
    pid_t pid; /**< The PID of the process that locked a segment */
    int fd; /**< The file descriptor of the locked file */
};

/**
 * @brief The locked segment of a file
 */
struct rl_lock {
    off_t start; /**< The beginning of the segment */
    off_t len; /**< The length of the segment */
    short type; /**< The type (F_RDLCK, F_WRLCK) of the lock */
    size_t nb_owners; /**< The number of owners of the lock */
    rl_owner lock_owners[RL_MAX_OWNERS]; /**< The owners of the lock */
};

/**
 * @brief The locks on an open file description
 */
struct rl_open_file {
    int nb_locks; /**< The number of locks */
    pthread_mutex_t mutex; /**< The exclusive lock on the open file */
    rl_lock lock_table[RL_MAX_LOCKS]; /**< The locks on the open file */
};

/**
 * @brief The open file description
 */
struct rl_descriptor {
    int fd; /**< The open file descriptor as in the descriptor table */
    rl_open_file *file; /**< The locks on the open file */
};

/**
 * @brief All the open file descriptions of a process
 */
struct rl_all_files {
    int nb_files; /**< The number of open file descriptions */
    rl_open_file *open_files[RL_MAX_FILES]; /**< The open file descriptions */
};

/******************************************************************************/

/**
 * @brief Initializes `pmutex` for process sync
 * @param pmutex the mutex to initialize
 * @return 0 if the initialization was successfull, the error code otherwise
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

/**
 * @brief Checks if `owner` is free
 * @param owner the owner to check
 * @return 1 if it is free, 0 otherwise
 */
static int is_owner_free(rl_owner *owner) {
    return owner != NULL && owner->fd == RL_FREE_OWNER;
}

/**
 * @brief Erases `owner` if possible
 * @param owner the owner to erase
 */
static void erase_owner(rl_owner *owner) {
    if (owner != NULL) {
        owner->pid = (pid_t) RL_FREE_OWNER;
        owner->fd = RL_FREE_OWNER;
    }
}

/**
 * @brief Moves the owners of `lck` in order to fit in the first
 * `lck->nb_owners` cells of `lck` owner table
 *
 * This function does not use any locking mechanism, so be sure to have an
 * exclusive lock on the structure before organizing its owners in order to
 * preserve data integrity.
 *
 * @param lck the lck that contains the owners to organize
 * @return 0 if the owners were successfully organized, -1 on error
 */
static int organize_owners(rl_lock *lck) {
    if (lck == NULL || lck->nb_owners < 0 || lck->nb_owners > RL_MAX_OWNERS)
        return -1;

    for (int i = 0; i < lck->nb_owners; i++) {
        if (is_owner_free(&lck->lock_owners[i])) {
            int j = i + 1;
            while (j < RL_MAX_OWNERS && is_owner_free(&lck->lock_owners[j]))
                j++;
            if (j >= RL_MAX_OWNERS)
                return -1;
            lck->lock_owners[i] = lck->lock_owners[j];
            erase_owner(&lck->lock_owners[j]);
        }
    }
    return 0;
}

/**
 * @brief Checks if the owners are equal
 * @param o1 the first owner
 * @param o2 the second owner
 * @return 1 if they are equal, that is if o1.pid == o2.pid && o1.fd == o2.fd,
 * 0 otherwise
 */
static int equals(rl_owner o1, rl_owner o2) {
    return o1.pid == o2.pid && o1.fd == o2.fd;
}

/******************************************************************************/

/**
 * @brief Erases `lck` if possible
 * @param lck the lock to erase
 */
static void erase_lock(rl_lock *lck) {
    if (lck != NULL)
        lck->start = RL_FREE_LOCK;
}

/**
 * @brief Checks if `lck` is free
 * @param lck the lock to check
 * @return 1 if `lck` is free, 0 otherwise
 */
static int is_lock_free(rl_lock *lck) {
    return lck != NULL && lck->start == RL_FREE_LOCK;
}

/**
 * @brief Moves the locks of `file` in order to fit in the first
 * `file->nb_locks` cells of `file` lock table
 *
 * This function does not use any locking mechanism, so be sure to have an
 * exclusive lock on the structure before organizing its lock in order to
 * preserve data integrity.
 *
 * @param file the file that contains the locks to organize
 * @return 0 if the locks were successfully organized, -1 on error
 */
static int organize_locks(rl_open_file *file) {
    if (file == NULL || file->nb_locks < 0 || file->nb_locks > RL_MAX_LOCKS)
        return -1;

    for (int i = 0; i < file->nb_locks; i++) {
        if (is_lock_free(&file->lock_table[i])) {
            int j = i + 1;
            while (j < RL_MAX_LOCKS && is_lock_free(&file->lock_table[j]))
                j++;
            if (j >= RL_MAX_LOCKS)
                return -1;
            file->lock_table[i] = file->lock_table[j];
            erase_lock(&file->lock_table[j]);
        }
    }
    return 0;
}

/**
 * @brief Converts `from` into a `struct flock` and puts the result in `to`
 * @param from the original `rl_lock`
 * @param to the conversion of `from` to a `struct flock`
 * @return 0 on success, -1 on error
 */
static void rl_lock_to_flock(const rl_lock *from, struct flock *to) {
    to->l_type = from->type;
    to->l_whence = SEEK_SET;
    to->l_start = from->start;
    to->l_len = from->len;
}

/******************************************************************************/

/**
 * @brief Deletes every owner that matches the given criteria in the given file
 *
 * This function does not use any locking mechanism. If `crit` returns a value
 * > 0, the owner given as first parameter of the function is erased from the
 * lock owners table of the currently considered lock of the file. If `crit`
 * returns 0, nothing is done. If it returns -1, the function quits on error.
 * After each removal, the owners in the lock owner table are reorganized, so as
 * the locks if there are no owners left.
 *
 * @param file the file that contains the lock owners to remove
 * @param crit a function that take two lock owners and returns an integer
 * @param owner_crit the owner used as second parameter of `crit`
 * @return 0 if the owner removal was succesful, -1 on error.
 */
static int delete_owner_on_criteria(rl_open_file *file,
                                    int (*crit)(rl_owner, rl_owner), rl_owner owner_crit) {
    if (crit == NULL || file == NULL || file->nb_locks < 0
        || file->nb_locks > RL_MAX_LOCKS)
        return -1;

    int locks_count = file->nb_locks;
    for (int i = 0; i < file->nb_locks; i++) {
        int owners_count = file->lock_table[i].nb_owners;
        for (int j = 0; j < file->lock_table[i].nb_owners; j++) {
            rl_owner *cur = &file->lock_table[i].lock_owners[j];
            int res = crit(*cur, owner_crit);
            if (res > 0) {
                erase_owner(cur);
                owners_count--;
            } else if (res == -1)
                return -1;
        }
        file->lock_table[i].nb_owners = owners_count;
        if (organize_owners(&file->lock_table[i]) < 0)
            return -1;
        if (owners_count == 0) {
            erase_lock(&file->lock_table[i]);
            locks_count--;
        }
    }
    file->nb_locks = locks_count;
    if (organize_locks(file) < 0)
        return -1;
    return 0;
}

/******************************************************************************/

/**
 * @brief Closes the given locked file descriptor
 *
 * This function removes from each lock of the descripted open file the owner
 * `{getpid(), lfd.fd}` if present. After deletion, the lock owners of each lock
 * are reorganized, as each lock of the lock table of the open file description.
 * The `close()` operation is made only if the previous operations are
 * successful.
 *
 * @param lfd the locked file descriptor to close
 * @return 0 if `lfd` was successfully closed, -1 on error
 */
int rl_close(rl_descriptor lfd) {
    /* check descriptor validity */
    if (lfd.fd < 0 || lfd.file == NULL)
        return -1;

    /* take lock on open file */
    int err = pthread_mutex_lock(&lfd.file->mutex);
    if (err != 0)
        return -1;

    rl_owner lfd_owner = {.pid = getpid(), .fd = lfd.fd};
    if (delete_owner_on_criteria(lfd.file, equals, lfd_owner) < 0)
        return -1;

    if (close(lfd.fd) == -1)
        return -1;

    err = pthread_mutex_unlock(&lfd.file->mutex);
    if (err != 0)
        return -1;
    return 0;
}

/******************************************************************************/

/**
 * @brief Initializes the library
 * 
 * You must call this function before using the library.
 *
 * @return always 0
 */
int rl_init_library() {
    rla.nb_files = 0;
    for (int i = 0; i < RL_MAX_FILES; i++)
        rla.open_files[i] = RL_FREE_FILE;
    return 0;
}

/******************************************************************************/

/**
 * @brief Adds the given open file to the open file descriptions of this process
 *
 * Fails if rla is full.
 *
 * @parm rlo the open file to add
 * @return 0 on success, -1 on error
 */
static int add_to_rla(rl_open_file *rlo) {
    if (rla.nb_files >= RL_MAX_FILES)
        return -1;
    rla.open_files[rla.nb_files] = rlo;
    rla.nb_files++;
    return 0;
}

/**
 * @brief Opens the file at the given path
 *
 * Opens `path` with the open() system call (identical parameters). Also does
 * the memory projection of the `rl_open_file` associated with the file at path,
 * creating the shared memory object if it doesn't exist. Returns the
 * corresponding `rl_descriptor`.
 *
 * @param path the relative or absolute path to the file
 * @param oflag the flags passed to `open()`
 * @param ... the mode (permissions) for the new file, required if O_CREAT flag
 *            is specified
 * @return the rl_descriptor containing the file descriptor returned by open()
 *         and a pointer to the rl_open_file associated to the file, or an
 *         rl_descriptor containing fd -1 and rl_open_file pointer NULL on error
 */
rl_descriptor rl_open(const char *path, int oflag, ...) {
    rl_descriptor err_desc = {.fd = -1, .file = NULL};

    if (rla.nb_files >= RL_MAX_FILES) {
        errno = EMFILE;
        return err_desc;
    }
    
    va_list va;
    va_start(va, oflag);

    int open_res;
    if (oflag & O_CREAT)
        open_res = open(path, oflag, va_arg(va, mode_t));
    else
        open_res = open(path, oflag);

    va_end(va);

    if (open_res == -1)
        return err_desc;
    
    struct stat st;
    int fstat_res = fstat(open_res, &st);
    if (fstat_res == -1) {
        close(open_res);
        return err_desc;
    }

    char shm_path[256];
    int snprintf_res = snprintf(shm_path, 256, "/%s_%lu_%lu", SHM_PREFIX,
                                st.st_dev, st.st_ino);
    if (snprintf_res < 0) {
        close(open_res);
        errno = ECANCELED;
        return err_desc;
    }

    int shm_res = shm_open(shm_path, O_RDWR, 0);
    rl_open_file *rlo = NULL;
    // Problem: process 1 creates the shm and is paused before initializing it
    // then process 2 comes here and the shm exists but is not initialized
    if (shm_res >= 0) {
        rlo = mmap(NULL, sizeof(rl_open_file), PROT_READ | PROT_WRITE,
                   MAP_SHARED, shm_res, 0);
        if (rlo == MAP_FAILED) {
            close(open_res);
            close(shm_res);
            return err_desc;
        }
    } else { // We create the shm
        int shm_res2 = shm_open(shm_path, O_RDWR | O_CREAT,
                                S_IRWXU | S_IRWXG | S_IRWXO);
        if (shm_res2 == -1) {
            close(open_res);
            shm_unlink(shm_path);
            return err_desc;
        }
        
        int trunc_res = ftruncate(shm_res2, sizeof(rl_open_file));
        if (trunc_res == -1) {
        error:
            close(open_res);
            close(shm_res2);
            shm_unlink(shm_path);
            return err_desc;
        }

        rlo = mmap(NULL, sizeof(rl_open_file), PROT_READ | PROT_WRITE,
                   MAP_SHARED, shm_res2, 0);
        if (rlo == MAP_FAILED)
            goto error;

        if (initialize_mutex(&rlo->mutex))
            goto error;
        if (pthread_mutex_lock(&rlo->mutex)) 
            goto error;

        rlo->nb_locks = 0;
        for (int i = 0; i < RL_MAX_LOCKS; i++) {
            erase_lock(&rlo->lock_table[i]);
            for (int j = 0; j < RL_MAX_OWNERS; j++)
                erase_owner(&rlo->lock_table[i].lock_owners[j]);
        }

        if (pthread_mutex_unlock(&rlo->mutex))
            goto error;

        if (add_to_rla(rlo) == -1) {
            close(open_res);
            close(shm_res2);
            return err_desc;
        }
    }

    rl_descriptor desc = {.fd = open_res, .file = rlo};
    return desc;
}

/**
 * @brief Checks if the segment [s1, s1 + l1[ and [s2, s2 + l2[ overlap
 * @param s1 the start of the first segment
 * @param l1 the length of the first segment
 * @param s2 the start of the second segment
 * @param l2 the length of the second segment
 * @return 1 if the segments overlap, 0 otherwise
 */
static int seg_overlap(off_t s1, off_t l1, off_t s2, off_t l2) {
    if (l1 == 0)
        return (l2 == 0) || (l2 > 0 && s2 + l2 - 1 >= s1);

    if (s2 >= s1)
        return s2 < s1 + l1;
    else
        return l2 == 0 || s2 + l2 > s1;
}

/**
 * @brief Checks if the lock is owned by an rl_owner different than owner
 * 
 * This function does not use any locking mechanism to secure the access to
 * lock, nor tests if owner is in fact an owner of lock.
 * 
 * @param lock the lock to check
 * @param owner the owner to compare the lock owners with
 * @return 1 if the lock is owned by a different owner, 0 otherwise
 */
static int has_different_owner(const rl_lock *lock, rl_owner owner) {
    for (int i = 0; i < RL_MAX_OWNERS; i++) {
        rl_owner other = lock->lock_owners[i];
        if (!is_owner_free(&other)) {
            if (!equals(other, owner))
                return 1;
        }
    }
    return 0;
}

/**
 * @brief Computes the starting offset of the lock of the file denoted by fd.
 * @param lck a lock on a region
 * @param fd the file descriptor of the file to lock
 * @return the starting offset of the region or -1 if it could not be determined
 */
static off_t get_start(struct flock *lck, int fd) {
    if (lck == NULL)
        return -1;

    if (lck->l_whence == SEEK_SET) {
        if (lck->l_start >= 0)
            return lck->l_start;
        else
            return -1; // before beginning of file !
    } else if (lck->l_whence == SEEK_CUR || lck->l_whence == SEEK_END) {
        off_t cur = lseek(fd, 0, SEEK_CUR); // current offset
        if (cur == -1)
            return -1;
        off_t pos = lseek(fd, lck->l_start, lck->l_whence); // lock offset
        if (pos == -1)
            return -1;
        if (lseek(fd, cur, SEEK_SET) == -1)
            return -1;
        return pos;
    }
    return -1;
}

/**
 * @brief Checks if the given lock can be put on the given descriptor
 * 
 * This function only checks for conflicting locks, it doesn't verify if the
 * lock table is big enough for the new locks. This is done in other functions.
 * This function does not use any locking mechanism, so be sure to take the lock
 * before entering this function in order to verify mutual exclusion.
 *
 * @param lck the lock to put
 * @param lfd the file on which to put the lock
 * @return 1 if the lock is applicable, 0 if it is not, -1 if an error occured.
 * If the lock is not applicable because of a lock put by a process that has
 * died and has not removed its locks, returns the pid of that process.
 */
static pid_t is_lock_applicable(struct flock *lck, rl_descriptor lfd) {
    if (lck == NULL || lfd.file == NULL || lck->l_len <= 0)
        return -1;

    off_t start = get_start(lck, lfd.fd);
    if (start == -1)
        return -1;

    if (lck->l_type == F_UNLCK)
        return 1;

    rl_open_file *file = lfd.file;
    if (file->nb_locks < 0 || file->nb_locks > RL_MAX_LOCKS)
        return -1;

    rl_owner lfd_owner = {.pid = getpid(), .fd = lfd.fd};

    for (int i = 0; i < file->nb_locks; i++) {
        rl_lock *cur = &file->lock_table[i];

        /* if locks overlap check for conflicts */
        if (seg_overlap(cur->start, cur->len, start, lck->l_len)) {
            if (cur->type == F_WRLCK || lck->l_type == F_WRLCK) {
                if (has_different_owner(cur, lfd_owner)) {
                    /* check if owner is still alive */
                    for (int j = 0; j < cur->nb_owners; j++) {
                        if (!equals(lfd_owner, cur->lock_owners[j])) {
                            if (kill(cur->lock_owners[j].pid, 0) == -1
                                && errno == ESRCH) {
                                return cur->lock_owners[j].pid;
                            } else {
                                return 0;
                            }
                        }
                    }

                    /* there is a different owner that has not been found */
                    return -1;
                }
            }
        }
    }
    return 1;
}

/**
 * @brief Checks if `ol` and `or` have the same PID
 * @param ol the left owner
 * @param or the right owner
 * @return 1 if `ol` and `or` have the same PID, 0 otherwise
 */
static int same_pid(rl_owner ol, rl_owner or) {
    return ol.pid == or.pid;
}

/**
 * @brief Removes the locks owned by the process of given PID in the given
 * rl_open_file
 *
 * This function does not use any locking mechanism, be sure that mutual
 * exclusion is assured before using it. If after removal a lock is empty, it is
 * also deleted. The owners of every modified lock are reorganized, so as the
 * locks of the file.
 *
 * @param pid the PID of the process that owns the locks to remove
 * @param file the file that contains the locks to remove
 * @return 0 on success, -1 on error
 */
static int remove_locks_of(pid_t pid, rl_open_file *file) {
    if (pid <= 0 || file == NULL || file->nb_locks < 0
        || file->nb_locks > RL_MAX_LOCKS)
        return -1;

    rl_owner cmp = {.pid = pid, .fd = 0};
    if (delete_owner_on_criteria(file, same_pid, cmp) < 0)
        return -1;
    return 0;
}

/**
 * @brief Adds `new` to the owners table of `lck` if possible
 * @param new the owner to add
 * @param lck the lock to which to add the new owner
 * @return 0 if `new` was succesfully added, -1 if it could not be added
 */
static int add_owner(rl_owner new, rl_lock *lck) {
    if (new.pid < 0 || new.fd < 0 || lck == NULL || lck->nb_owners < 0
        || lck->nb_owners + 1 > RL_MAX_OWNERS)
        return -1;
    lck->lock_owners[lck->nb_owners] = new;
    lck->nb_owners++;
    return 0;
}

/**
 * @brief Checks if `owner` is an owner of `lck`
 * @param owner the owner that might own `lck`
 * @param lck the lock that might be owned by `owner`
 * @return 1 if `owner` is an owner of `lck`, 0 if it is not
 */
static int is_owner_of(rl_owner owner, rl_lock *lck) {
    for (int i = 0; i < lck->nb_owners; i++) {
        if (equals(owner, lck->lock_owners[i]))
            return 1;
    }
    return 0;
}

/**
 * @brief Adds `new` to the locks of `file` if possible, where `first` is the
 * initial owner of `new`
 *
 * This function should be use when `new` is not already a lock of `file`. The
 * owners that might be stored in `new` are erased.
 *
 * @param new the lock to add
 * @param file the file in which to add `new`
 * @param first the initial owner of `new`
 */
static int add_lock(rl_lock *new, rl_open_file *file, rl_owner first) {
    if (new == NULL || file == NULL || first.pid < 0 || first.fd < 0
        || (new->type != F_RDLCK && new->type != F_WRLCK) || new->start < 0
        || new->len <= 0 || file->nb_locks + 1 > RL_MAX_LOCKS
        || file->nb_locks < 0)
        return -1;
    file->lock_table[file->nb_locks] = *new;
    rl_lock *tmp = &file->lock_table[file->nb_locks];
    for (int i = 0; i < RL_MAX_OWNERS; i++)
        erase_owner(&tmp->lock_owners[i]);
    file->nb_locks++;
    tmp->nb_owners = 0;
    if (add_owner(first, tmp) == -1)
        return -1;
    return 0;
}

/**
 * @brief Gets the lock in `file` that starts at the same offset, has the same
 * length and is of the same type than `lck`
 * @param file the file that might contain `lck`
 * @param lck the lock to find in `file`
 * @return A pointer to the corresponding lock in file or NULL if it was not
 * found
 */
static rl_lock *find_lock(rl_open_file *file, rl_lock *lck) {
    if (file == NULL || lck == NULL || file->nb_locks < 0 || lck->start < 0
        || lck->len <= 0 || (lck->type != F_WRLCK && lck->type != F_RDLCK))
        return NULL;
    for (int i = 0; i < file->nb_locks; i++) {
        rl_lock *tmp = &file->lock_table[i];
        if (tmp->start == lck->start && tmp->len == lck->len
            && tmp->type == lck->type)
            return tmp;
    }
    return NULL;
}

/**
 * @brief Unlocks the region delimited by `lck` of the open file pointed by
 * `lfd`
 *
 * `lck` must be of type `F_UNLCK`, the region to lock must be finite (not
 * extensible) and must start at or after the beginning of the file. This
 * function does not use any locking mechanism, ensure mutual exclusion before
 * the call.
 *
 * @param lfd the file descriptor to unlock
 * @param lck the region to unlock
 * @return 0 on success, -1 on error
 */
static int apply_unlock(rl_descriptor lfd, struct flock *lck) {
    if (lfd.fd < 0 || lfd.file == NULL || lck == NULL || lck->l_type != F_UNLCK
        || lck->l_len <= 0 || lfd.file->nb_locks < 0)
        return -1;

    off_t lck_start = get_start(lck, lfd.fd);
    if (lck_start == -1)
        return -1;

    int nb_locks = lfd.file->nb_locks;
    size_t nb_new_locks = 0;
    rl_lock new_locks[2 * nb_locks];
    size_t nb_locks_to_remove = 0;
    size_t locks_to_remove[nb_locks];
    rl_owner lfd_owner = {.pid = getpid(), .fd = lfd.fd};
    for (int i = 0; i < nb_locks; i++) {
        rl_lock *cur = &lfd.file->lock_table[i];
        if (is_owner_of(lfd_owner, cur)
            && seg_overlap(lck_start, lck->l_len, cur->start, cur->len)) {
            locks_to_remove[nb_locks_to_remove] = i;
            nb_locks_to_remove++;
            if (cur->start < lck_start /* strict unlock of cur middle */
                && cur->start + cur->len > lck_start + lck->l_len) {
                rl_lock l1, l2;
                l1.type = cur->type;
                l1.start = cur->start;
                l1.len = lck_start - cur->start;
                new_locks[nb_new_locks] = l1;
                nb_new_locks++;

                l2.type = cur->type;
                l2.start = lck_start + lck->l_len;
                l2.len = (cur->start + cur->len) - (lck_start + lck->l_len);
                new_locks[nb_new_locks] = l2;
                nb_new_locks++;
            } else if (cur->start >= lck_start /* unlock cur entirely */
                       && cur->start + cur->len <= lck_start + lck->l_len)
                continue;
            else if (cur->start < lck_start /* unlock end of cur */
                     && cur->start + cur->len <= lck_start + lck->l_len) {
                rl_lock l1;
                l1.type = cur->type;
                l1.start = cur->start;
                l1.len = lck_start - cur->start;
                new_locks[nb_new_locks] = l1;
                nb_new_locks++;
            } else { /* unlock beginning of cur */
                rl_lock l1;
                l1.type = cur->type;
                l1.start = lck_start + lck->l_len;
                l1.len = (cur->start + cur->len) - (lck_start + lck->l_len);
                new_locks[nb_new_locks] = l1;
                nb_new_locks++;
            }
        }
        if (nb_new_locks + nb_locks > RL_MAX_LOCKS)
            return -1;
    }
    for (int i = 0; i < nb_locks_to_remove; i++) {
        size_t ind = locks_to_remove[i];
        rl_lock *rlck = &lfd.file->lock_table[ind];
        if (rlck->nb_owners == 1) {
            erase_lock(rlck);
            lfd.file->nb_locks--;
        } else {
            size_t nb_owners = rlck->nb_owners;
            for (int j = 0; j < rlck->nb_owners; j++) {
                if (equals(lfd_owner, rlck->lock_owners[j])) {
                    erase_owner(&rlck->lock_owners[j]);
                    nb_owners--;
                }
            }
            rlck->nb_owners = nb_owners;
            if (organize_owners(rlck) == -1)
                return -1;
        }
    }
    if (organize_locks(lfd.file) == -1)
        return -1;
    for (int i = 0; i < nb_new_locks; i++) {
        rl_lock *tmp = find_lock(lfd.file, &new_locks[i]);
        if (tmp != NULL) {
            if (add_owner(lfd_owner, tmp) == -1)
                return -1;
        } else {
            if (add_lock(&new_locks[i], lfd.file, lfd_owner) == -1)
                return -1;
        }
    }
    return 0;
}

/**
 * @brief Locks the region specified by `lck` of the open file pointed by
 * `lfd`
 *
 * The region to lock must be finite (not
 * extensible) and must start at or after the beginning of the file. This
 * function does not use any locking mechanism, ensure mutual exclusion before
 * the call.
 *
 * @param lfd the file descriptor to lock
 * @param lck the region to lock
 * @return 0 on success, -1 on error
 */
static int apply_rw_lock(rl_descriptor lfd, struct flock *lck) {
    if (lfd.file->nb_locks + 2 > RL_MAX_LOCKS)
        return -1;

    off_t lck_start = get_start(lck, lfd.fd);
    if (lck_start == -1)
        return -1;

    struct flock unlock;
    unlock.l_type = F_UNLCK;
    unlock.l_whence = SEEK_SET;
    unlock.l_start = lck_start;
    unlock.l_len = lck->l_len;
    if (apply_unlock(lfd, &unlock) == -1)
        return -1;
    
    rl_owner lfd_owner = {.pid = getpid(), .fd = lfd.fd};
    rl_lock *left = NULL;
    rl_lock *right = NULL;
    for (int i = 0; i < lfd.file->nb_locks; i++) {
        rl_lock *cur = &lfd.file->lock_table[i];
        if (cur->type != lck->l_type || !is_owner_of(lfd_owner, cur))
            continue;
        if (cur->start + cur->len == lck_start)
            left = cur;
        else if (cur->start == lck_start + lck->l_len)
            right = cur;
    }

    int unlock_left = 0;
    int unlock_right = 0;
    rl_lock tmp;
    tmp.type = lck->l_type;
    tmp.start = lck_start;
    tmp.len = lck->l_len;
    if (left != NULL && right != NULL) {
        tmp.len += left->len + right->len;
        tmp.start = left->start;
        unlock_left = 1;
        unlock_right = 1;
    } else if (left != NULL) {
        tmp.len += left->len;
        tmp.start = left->start;
        unlock_left = 1;
    } else if (right != NULL) {
        tmp.len += right->len;
        unlock_right = 1;
    }

    if (unlock_left) {
        struct flock left2;
        rl_lock_to_flock(left, &left2);
        left2.l_type = F_UNLCK;
        if (apply_unlock(lfd, &left2) == -1)
            return -1;
    }
    if (unlock_right) {
        struct flock right2;
        rl_lock_to_flock(right, &right2);
        right2.l_type = F_UNLCK;
        if (apply_unlock(lfd, &right2) == -1)
            return -1;
    }

    rl_lock *tmp2 = find_lock(lfd.file, &tmp);
    if (tmp2 != NULL) {
        if (add_owner(lfd_owner, tmp2) == -1)
            return -1;
    } else {
        if (add_lock(&tmp, lfd.file, lfd_owner) == -1)
            return -1;
    }
    
    return 0;
}

/**
 * @brief 
 * @param lfd 
 * @param cmd 
 * @param lck 
 * @return int 
 */
int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck) {
    return -1;   
}

/******************************************************************************/

/**
 * @brief Adds new_owner as a lock owner of every lock where
 * `lfd_owner = {.pid = getpid(), .fd = lfd.fd}` is also an owner
 * @param lfd the file descriptor where to add `new_owner`
 * @param new_owner the owner to add
 * @return 0 on success, -1 on error
 */
static int dup_owner(rl_descriptor lfd, rl_owner new_owner) {
    rl_owner lfd_owner = {.pid = getpid(), .fd = lfd.fd};
    for (int i = 0; i < lfd.file->nb_locks; i++) {
        rl_lock *tmp = &lfd.file->lock_table[i];
        int code = is_owner_of(lfd_owner, tmp);
        if (code == -1)
            return -1;
        if (code) {
            if (add_owner(new_owner, tmp) == -1)
                return -1;
        }
    }
    return 0;
}

/**
 * @brief Duplicates `lfd` using the lowest numbered available file descriptor
 * @param lfd the locked file description to duplicate
 * @return a duplication of `lfd` on success, {.fd = -1, .file = NULL} on error
 */
rl_descriptor rl_dup(rl_descriptor lfd) {
    rl_descriptor err = {.fd = -1, .file = NULL};

    if (lfd.fd < 0 || lfd.file == NULL)
        return err;

    int new_fd = dup(lfd.fd);
    if (new_fd == -1)
        return err;
    
    rl_owner new_owner = {.pid = getpid(), .fd = new_fd};
    if (dup_owner(lfd, new_owner) == -1) {
        close(new_fd);
        return err;
    }

    rl_descriptor res = {.fd = new_fd, .file = lfd.file};
    return res;
}

/**
 * @brief Duplicates `lfd` using `new_fd`
 * @param lfd the locked file description to duplicate
 * @param new_fd the open file description to use for the duplication
 * @return {.fd = new_fd, .file = lfd.file} on succes, {.fd = -1, .file = NULL}
 * on error
 */
rl_descriptor rl_dup2(rl_descriptor lfd, int new_fd) {
    rl_descriptor err = {.fd = -1, .file = NULL};

    if (lfd.fd < 0 || lfd.file == NULL)
        return err;

    if (lfd.fd == new_fd)
        return lfd;

    int code = dup2(lfd.fd, new_fd);
    if (code == -1)
        return err;
    
    rl_owner new_owner = {.pid = getpid(), .fd = new_fd};
    if (dup_owner(lfd, new_owner) == -1) {
        close(new_fd);
        return err;
    }

    rl_descriptor res = {.fd = new_fd, .file = lfd.file};
    return res;
}

/******************************************************************************/

pid_t rl_fork() {
    pid_t pid = fork();
    if (pid == (pid_t) -1)
        return -1;
    
    if (pid == 0) {
        pid_t parent = getppid();
        /*
         * pid_t child = getpid()
         * Pour i de 0 à nb_files:
         *     file := open_files[i]
         *     Pour j de 0 à file->nb_locks:
         *         lck := file->lock_table[j]
         *         nb_owners := lck->nb_owners
         *         Pour k de 0 à nb_owners:
         *             Si lck->lock_owner[k].pid = parent:
         *                 Ajouter {.pid = child, .fd = lck->lock_owner[k].fd}
         *  Retourner 0
         */
    }

    return pid;
}
