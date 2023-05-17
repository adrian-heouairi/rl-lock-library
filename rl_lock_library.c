/*
 * Adrian HEOUAIRI
 * Guillermo MORON USON
 */

#define _XOPEN_SOURCE 500

#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>

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
    off_t starting_offset; /**< The beginning of the segment */
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
    rl_open_file *file; /**< The locks associated to the open file description */
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

/******************************************************************************/

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
        lck->starting_offset = RL_FREE_LOCK;
}

/**
 * @brief Checks if `lck` is free
 * @param lck the lock to check
 * @return 1 if `lck` is free, 0 otherwise
 */
static int is_lock_free(rl_lock *lck) {
    return lck != NULL && lck->starting_offset == RL_FREE_LOCK;
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
    int nb_locks = lfd.file->nb_locks;
    for (int i = 0; i < lfd.file->nb_locks; i++) {
        int nb_owners = lfd.file->lock_table[i].nb_owners;
        for (int j = 0; j < lfd.file->lock_table[i].nb_owners; j++) {
            if (equals(lfd_owner, lfd.file->lock_table[i].lock_owners[j])) {
                erase_owner(&lfd.file->lock_table[i].lock_owners[j]);
                nb_owners--;
            }
        }
        lfd.file->lock_table[i].nb_owners = nb_owners;
        if (organize_owners(&lfd.file->lock_table[i]) == -1)
            return -1;
        if (lfd.file->lock_table[i].nb_owners == 0) {
            erase_lock(&lfd.file->lock_table[i]);
            nb_locks--;
        }
    }

    lfd.file->nb_locks = nb_locks;
    if (organize_locks(lfd.file) == -1)
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
