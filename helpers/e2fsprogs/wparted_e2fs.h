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
/* As config.h will say; the system headers included below must agree. */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
/* rand_s() (RtlGenRandom) needs this before the first <stdlib.h>. */
#define _CRT_RAND_S
/* gmtime_r() and localtime_r() from <time.h>. */
#define _POSIX_THREAD_SAFE_FUNCTIONS 200112L
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

/* Strong randomness for UUIDs and hash seeds: without it libuuid would fall
 * back to rand() seeded from the time and process id on Windows. */
static inline int wparted_getentropy(void* buf, size_t len)
{
	unsigned char* p = (unsigned char*) buf;
	while (len)
	{
		unsigned int v;
		size_t n = len < sizeof(v) ? len : sizeof(v);
		if (rand_s(&v) != 0)
			return -1;
		memcpy(p, &v, n);
		p += n;
		len -= n;
	}
	return 0;
}
#define getentropy wparted_getentropy
#define HAVE_GETENTROPY 1

static inline long wparted_random(void)
{
	unsigned int v = 0;
	rand_s(&v);
	return (long) (v & 0x7fffffff);
}
static inline void wparted_srandom(unsigned int seed) { (void) seed; }
#define random  wparted_random
#define srandom wparted_srandom

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
#ifndef SIGCHLD
#define SIGCHLD 17
#endif
#ifndef SI_USER
#define SI_USER 0
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
/* e2fsck can pipe its log to a program (e2fsck.conf); not on Windows. */
static inline int wparted_fork(void)         { return -1; }
static inline int wparted_daemon(int a, int b) { (void)a; (void)b; return -1; }
static inline int wparted_pipe(int fds[2])   { (void)fds; return -1; }
#define fork   wparted_fork
#define daemon wparted_daemon
#define pipe   wparted_pipe
/* e2fsck's resource tracking (-t) and log file names (%h). */
static inline void* wparted_sbrk(intptr_t n) { (void)n; return (void*)0; }
#define sbrk wparted_sbrk
static inline int wparted_gethostname(char* name, int len)   /* as <winsock2.h> */
{
	if (len)
	{
		strncpy(name, "localhost", len - 1);
		name[len - 1] = 0;
	}
	return 0;
}
#define gethostname wparted_gethostname
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
