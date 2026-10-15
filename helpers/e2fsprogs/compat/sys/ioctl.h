/* WParted helpers: no ioctl on Windows. */
#ifndef WPARTED_COMPAT_SYS_IOCTL_H
#define WPARTED_COMPAT_SYS_IOCTL_H
static inline int ioctl(int fd, unsigned long req, ...) { (void)fd; (void)req; return -1; }
#endif
