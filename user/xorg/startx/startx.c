/* OpenSillicon graphical session supervisor. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile int stopping;
static void stop(int signo) { (void)signo; stopping = 1; }

static int display_socket(void)
{
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	static const char path[] = "/tmp/.X11-unix/X0";
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	memcpy(address.sun_path + 1, path, sizeof(path) - 1);
	if (connect(fd, (struct sockaddr *)&address,
	    offsetof(struct sockaddr_un, sun_path) + sizeof(path)) < 0) {
		close(fd); return -1;
	}
	return fd;
}

static pid_t launch(char *const argv[])
{
	pid_t pid = fork();
	if (pid == 0) {
		signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL);
		execv(argv[0], argv);
		fprintf(stderr, "startx: cannot execute %s: %s\n", argv[0], strerror(errno));
		_exit(127);
	}
	return pid;
}

int main(void)
{
	char *server_args[] = {"/bin/Xopensillicon", ":0", NULL};
	char *wm_args[] = {"/bin/twm", "-display", ":0", NULL};
	char *term_args[] = {"/bin/xterm", "-display", ":0", "-geometry", "80x24+40+40", NULL};
	pid_t children[3] = {-1, -1, -1};
	int result = 1, fd, status;
	/* Check executables before taking over the framebuffer. */
	for (unsigned i = 0; i < 3; i++) {
		const char *path = i == 0 ? server_args[0] : i == 1 ? wm_args[0] : term_args[0];
		fd = open(path, 0);
		if (fd < 0) { fprintf(stderr, "startx: missing %s\n", path); return 1; }
		close(fd);
	}
	fd = display_socket();
	if (fd >= 0) { close(fd); fputs("startx: display :0 already active\n", stderr); return 1; }
	if (signal(SIGINT, stop) == SIG_ERR || signal(SIGTERM, stop) == SIG_ERR)
		return 1;
	children[0] = launch(server_args);
	if (children[0] < 0) goto cleanup;
	for (unsigned i = 0; i < 200 && !stopping; i++) {
		fd = display_socket();
		if (fd >= 0) break;
		struct timespec delay = {0, 50000000};
		nanosleep(&delay, NULL);
	}
	if (fd < 0 || stopping) {
		fputs("startx: display :0 did not become ready\n", stderr);
		goto cleanup;
	}
	close(fd);
	children[1] = launch(wm_args);
	if (children[1] < 0) goto cleanup;
	children[2] = launch(term_args);
	if (children[2] < 0) goto cleanup;
	puts("startx: TinyX, twm and xterm launched");
	while (!stopping) {
		pid_t pid = waitpid(-1, &status, 0);
		if (pid < 0) {
			if (errno == EINTR) continue;
			break;
		}
		for (unsigned i = 0; i < 3; i++)
			if (children[i] == pid) children[i] = -1;
		result = WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
		break;
	}
cleanup:
	/* Clients leave first so they can still communicate with the server. */
	for (int i = 2; i >= 0; i--) {
		if (children[i] <= 0) continue;
		kill(children[i], SIGTERM);
		/* SIGKILL guarantees cleanup even if the child ignores SIGTERM. */
		struct timespec delay = {0, 100000000};
		nanosleep(&delay, NULL);
		kill(children[i], SIGKILL);
		while (waitpid(children[i], &status, 0) < 0 && errno == EINTR) {}
	}
	return result;
}
