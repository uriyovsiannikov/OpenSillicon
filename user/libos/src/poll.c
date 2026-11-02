#include <poll.h>
#include <fcntl.h>
#include <sys/select.h>

int
poll(struct pollfd *fds, nfds_t count, int timeout)
{
	fd_set read_set, write_set, except_set;
	struct timeval value, *value_pointer = 0;
	int max_fd = -1;
	int ready;
	int ready_count = 0;

	if (count != 0 && fds == 0)
		return -1;
	if (timeout < -1)
		return -1;
	FD_ZERO(&read_set);
	FD_ZERO(&write_set);
	FD_ZERO(&except_set);
	for (nfds_t i = 0; i < count; i++) {
		int fd = fds[i].fd;
		fds[i].revents = 0;
		if (fd < 0)
			continue;
		if (fd >= 32 || fcntl(fd, F_GETFL) < 0) {
			fds[i].revents = POLLNVAL;
			ready_count++;
			continue;
		}
		if (fds[i].events & POLLIN)
			FD_SET(fd, &read_set);
		if (fds[i].events & POLLOUT)
			FD_SET(fd, &write_set);
		FD_SET(fd, &except_set);
		if (fd > max_fd)
			max_fd = fd;
	}
	if (ready_count != 0)
		return ready_count;
	if (timeout >= 0) {
		value.tv_sec = timeout / 1000;
		value.tv_usec = (timeout % 1000) * 1000;
		value_pointer = &value;
	}
	ready = select(max_fd + 1, &read_set, &write_set, &except_set,
	    value_pointer);
	if (ready < 0)
		return -1;
	for (nfds_t i = 0; i < count; i++) {
		int fd = fds[i].fd;
		short revents = 0;
		if (fd < 0 || fd >= 32)
			continue;
		if ((fds[i].events & POLLIN) && FD_ISSET(fd, &read_set))
			revents |= POLLIN;
		if ((fds[i].events & POLLOUT) && FD_ISSET(fd, &write_set))
			revents |= POLLOUT;
		if (FD_ISSET(fd, &except_set))
			revents |= POLLERR;
		fds[i].revents = revents;
		if (revents != 0)
			ready_count++;
	}
	return ready_count;
}
