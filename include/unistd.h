/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_UNISTD_H
#define WPARTED_UNISTD_H

#if defined(_WIN32)
#include <io.h>
#include <process.h>
#include <direct.h>
#include <stdlib.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef access
#define access _access
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 0
#endif
#ifndef F_OK
#define F_OK 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#if defined(_MSC_VER)
#ifndef _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#define _SSIZE_T_DEFINED
#endif
#ifndef _PID_T_DEFINED
typedef int pid_t;
#define _PID_T_DEFINED
#endif
#endif

inline int usleep(unsigned int usec)
{
    Sleep(usec / 1000);
    return 0;
}

inline unsigned int sleep(unsigned int seconds)
{
    Sleep(seconds * 1000);
    return 0;
}

inline int getpid()
{
    return static_cast<int>(GetCurrentProcessId());
}

inline int getuid()
{
    return 0; // Root / Administrator equivalent on Windows
}

inline ssize_t readlink(const char* path, char* buf, size_t bufsiz)
{
    (void)path; (void)buf; (void)bufsiz;
    errno = ENOENT;
    return -1;
}

inline char* realpath(const char* path, char* resolved_path)
{
    return _fullpath(resolved_path, path, resolved_path ? _MAX_PATH : 0);
}

#endif // _WIN32

#endif // WPARTED_UNISTD_H
