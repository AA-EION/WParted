/* Copyright (C) 2026 WParted contributors
 *
 * This file may be redistributed under the terms of the GNU General Public
 * License, version 2 or later.
 */

/* Included before every e2fsprogs source file (-include) when building the
 * tools WParted bundles.  Routes every I/O manager the tools name to the
 * broker I/O manager (wparted_io.c), so they can only reach the partition
 * WParted gives them.  On Windows it also supplies the few POSIX pieces the
 * tools use and the helpers do not need: signal handling, terminal control
 * and resource accounting are no-ops (the job object ends the helper, and
 * progress goes through a pipe).
 */

#ifndef WPARTED_E2FS_H
#define WPARTED_E2FS_H

#define unix_io_manager    wparted_io_manager
#define windows_io_manager wparted_io_manager
#define unixfd_io_manager  wparted_io_manager

#ifdef _WIN32
#include <limits.h>
#include <signal.h>
#include <sys/types.h>

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#ifndef SIGUSR1
#define SIGUSR1 30
#endif
#ifndef SIGUSR2
#define SIGUSR2 31
#endif
#ifndef SIGBUS
#define SIGBUS  7
#endif
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGCONT
#define SIGCONT 18
#endif
#ifndef SIGTSTP
#define SIGTSTP 20
#endif
#ifndef SIGHUP
#define SIGHUP  1
#endif
#ifndef SIGQUIT
#define SIGQUIT 3
#endif
#ifndef SIGALRM
#define SIGALRM 14
#endif

typedef unsigned long wparted_sigset_t;
#define sigset_t wparted_sigset_t

typedef struct
{
	int   si_signo;
	int   si_code;
	int   si_errno;
	void* si_addr;
	int   si_pid;
	int   si_uid;
} siginfo_t;

struct sigaction
{
	void (*sa_handler)(int);
	void (*sa_sigaction)(int, siginfo_t*, void*);
	sigset_t sa_mask;
	int sa_flags;
};

#define SA_SIGINFO   4
#define SA_RESTART   0x10000000
#define SA_RESETHAND 0x80000000
#define SIG_BLOCK    0
#define SIG_UNBLOCK  1
#define SIG_SETMASK  2

static inline int sigemptyset(sigset_t* s)            { *s = 0; return 0; }
static inline int sigfillset(sigset_t* s)             { *s = ~0UL; return 0; }
static inline int sigaddset(sigset_t* s, int n)       { (void)s; (void)n; return 0; }
static inline int sigdelset(sigset_t* s, int n)       { (void)s; (void)n; return 0; }
static inline int sigprocmask(int h, const sigset_t* n, sigset_t* o) { (void)h; (void)n; if (o) *o = 0; return 0; }
static inline int sigaction(int n, const struct sigaction* a, struct sigaction* o)
{
	(void)n; (void)a;
	if (o)
	{
		o->sa_handler = 0;
		o->sa_sigaction = 0;
		o->sa_mask = 0;
		o->sa_flags = 0;
	}
	return 0;
}
static inline int wparted_kill(int pid, int sig) { (void)pid; (void)sig; return 0; }
#define kill wparted_kill

#ifndef S_ISLNK
#define S_ISLNK(m) 0
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(m) 0
#endif

static inline int getuid(void)  { return 0; }
static inline int geteuid(void) { return 0; }
static inline int getgid(void)  { return 0; }
static inline int getegid(void) { return 0; }
#endif /* _WIN32 */

#endif /* WPARTED_E2FS_H */
