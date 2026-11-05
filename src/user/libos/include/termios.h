#ifndef LIBOS_TERMIOS_H
#define LIBOS_TERMIOS_H

#include <sys/types.h>

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;
#define NCCS 32

struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_cc[32];
	speed_t c_ispeed;
	speed_t c_ospeed;
};

#define IGNBRK 0x0001U
#define BRKINT 0x0002U
#define IGNPAR 0x0004U
#define INPCK 0x0010U
#define PARMRK 0x0008U
#define ISTRIP 0x0020U
#define INLCR 0x0040U
#define IGNCR 0x0080U
#define ICRNL 0x0100U
#define IXON 0x0400U
#define OPOST 0x0001U
#define ONLCR 0x0004U
#define CSIZE 0x0030U
#define CS8 0x0030U
#define CS7 0x0020U
#define CREAD 0x0080U
#define PARENB 0x0100U
#define HUPCL 0x0400U
#define CLOCAL 0x0800U
#define CBAUD 0x0000100fU
#define B0 0U
#define B50 1U
#define B75 2U
#define B110 3U
#define B134 4U
#define B150 5U
#define B200 6U
#define B300 7U
#define B600 8U
#define B1200 9U
#define B1800 10U
#define B2400 11U
#define B4800 12U
#define B9600 13U
#define TAB0 0x0000U
#define TAB3 0x0004U
#define OCRNL 0x0008U
#define ONLRET 0x0020U
#define NLDLY 0x0100U
#define CRDLY 0x0600U
#define BSDLY 0x2000U
#define VTDLY 0x4000U
#define FFDLY 0x8000U
#define ISIG 0x0001U
#define ICANON 0x0002U
#define ECHO 0x0008U
#define ECHOE 0x0010U
#define ECHOK 0x0020U
#define ECHONL 0x0040U
#define NOFLSH 0x0080U
#define IEXTEN 0x8000U
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VEOL 11
#define VEOL2 16
#define VTIME 5
#define VMIN 6
#define VSTART 8
#define VSTOP 9
#define VLNEXT 15
#define VSUSP 10
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define TCOFLUSH 1
#define TCIFLUSH 0
#define TCIOFLUSH 2
#define _POSIX_VDISABLE ((cc_t)0)

int tcgetattr(int, struct termios *);
int tcsetattr(int, int, const struct termios *);
int tcflush(int, int);
pid_t tcgetpgrp(int);
int tcsetpgrp(int, pid_t);
speed_t cfgetispeed(const struct termios *);
speed_t cfgetospeed(const struct termios *);
int cfsetispeed(struct termios *, speed_t);
int cfsetospeed(struct termios *, speed_t);

#endif
