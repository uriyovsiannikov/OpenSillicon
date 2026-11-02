#ifndef LIBOS_SYS_IOCTL_H
#define LIBOS_SYS_IOCTL_H

#define FIONREAD 0x541B
#define FIONBIO 0x5421
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCFLSH 0x540B
struct winsize {
	unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel;
};
#define TIOCSRAW 0x4f530001
#define TIOCCANON 0x4f530002

int ioctl(int fd, unsigned long request, ...);

#endif
