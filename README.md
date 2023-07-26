# rl-lock-library

Computer science degree fourth year project. A library implementing locks similar to Linux OFD locks in user space using shm objects. Common system calls are replaced by a wrapper function, e.g. `fork()` becomes `rl_fork()`.
