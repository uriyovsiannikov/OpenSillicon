#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>

int
tcgetattr(int descriptor, struct termios *attributes)
{
	if (attributes == 0) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(descriptor, TCGETS, attributes);
}

int
tcsetattr(int descriptor, int action, const struct termios *attributes)
{
	if (attributes == 0 || action < TCSANOW || action > TCSAFLUSH) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(descriptor, TCSETS, attributes);
}

int tcflush(int descriptor, int queue)
{
	return ioctl(descriptor, TCFLSH, (void *)(long)queue);
}

speed_t cfgetispeed(const struct termios *attributes) { return attributes->c_ispeed; }
speed_t cfgetospeed(const struct termios *attributes) { return attributes->c_ospeed; }
int cfsetispeed(struct termios *attributes, speed_t speed)
{ attributes->c_ispeed = speed; return 0; }
int cfsetospeed(struct termios *attributes, speed_t speed)
{ attributes->c_ospeed = speed; return 0; }
