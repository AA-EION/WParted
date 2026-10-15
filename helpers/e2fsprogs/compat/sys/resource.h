/* WParted helpers: no resource accounting on Windows. */
#ifndef WPARTED_COMPAT_SYS_RESOURCE_H
#define WPARTED_COMPAT_SYS_RESOURCE_H
#include <sys/time.h>
struct rusage { struct timeval ru_utime; struct timeval ru_stime; long ru_maxrss; };
#define RUSAGE_SELF 0
static inline int getrusage(int who, struct rusage* u)
{
	(void)who;
	u->ru_utime.tv_sec = u->ru_utime.tv_usec = 0;
	u->ru_stime.tv_sec = u->ru_stime.tv_usec = 0;
	u->ru_maxrss = 0;
	return 0;
}
#endif
