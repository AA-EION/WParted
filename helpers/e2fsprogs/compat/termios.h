/* WParted helpers: no terminal control on Windows.  tcgetattr() fails, so
 * the tools take their non-interactive paths. */
#ifndef WPARTED_COMPAT_TERMIOS_H
#define WPARTED_COMPAT_TERMIOS_H
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
#define NCCS 32
struct termios
{
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t     c_cc[NCCS];
};
#define OPOST     0x0001
#define ONLCR     0x0004
#define ICANON    0x0002
#define ECHO      0x0008
#define ISIG      0x0001
#define VTIME     5
#define VMIN      6
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2
static inline int tcgetattr(int fd, struct termios* t) { (void)fd; (void)t; return -1; }
static inline int tcsetattr(int fd, int a, const struct termios* t) { (void)fd; (void)a; (void)t; return -1; }
#endif
