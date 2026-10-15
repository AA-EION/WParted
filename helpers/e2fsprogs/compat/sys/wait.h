/* WParted helpers: no child processes on Windows. */
#ifndef WPARTED_COMPAT_SYS_WAIT_H
#define WPARTED_COMPAT_SYS_WAIT_H
#define WIFEXITED(s)   1
#define WEXITSTATUS(s) (s)
#define WIFSIGNALED(s) 0
#define WTERMSIG(s)    0
#define WNOHANG 1
static inline int waitpid(int pid, int* st, int opt) { (void)pid; (void)opt; if (st) *st = 0; return -1; }
#endif
