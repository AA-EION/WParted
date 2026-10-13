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

#define access _access
#define R_OK 4
#define W_OK 2
#define X_OK 0
#define F_OK 0

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

typedef intptr_t ssize_t;
typedef int pid_t;

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

#endif // _WIN32

#endif // WPARTED_UNISTD_H
