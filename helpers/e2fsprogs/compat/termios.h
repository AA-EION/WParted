/* WParted helpers: no terminal control on Windows. */
#ifndef WPARTED_COMPAT_TERMIOS_H
#define WPARTED_COMPAT_TERMIOS_H
struct termios { unsigned c_lflag; unsigned c_iflag; unsigned c_cc[32]; };
#define ICANON 2
#define ECHO   8
#define VMIN   6
#define VTIME  5
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
static inline int tcgetattr(int fd, struct termios* t) { (void)fd; (void)t; return -1; }
static inline int tcsetattr(int fd, int a, const struct termios* t) { (void)fd; (void)a; (void)t; return -1; }
#endif
