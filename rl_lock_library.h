#ifndef _RL_LOCK_LIBRARY
#define _RL_LOCK_LIBRARY

#define _XOPEN_SOURCE 500

#include <sys/file.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>

typedef struct rl_owner rl_owner;
typedef struct rl_lock rl_lock;
typedef struct rl_open_file rl_open_file;
typedef struct rl_descriptor rl_descriptor;
typedef struct rl_all_files rl_all_files;

rl_descriptor rl_open(const char *path, int oflag, ...);
int rl_close(rl_descriptor lfd);
int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck);
rl_descriptor rl_dup(rl_descriptor lfd);
rl_descriptor rl_dup2(rl_descriptor lfd, int newd);
pid_t rl_fork();
int rl_init_library();

#endif
