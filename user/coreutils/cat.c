#include "../include/os.h"

#define BUFFER_SIZE 512U
#define PATH_SIZE 256U

static int
copy_descriptor(uint64_t descriptor)
{
	char buffer[BUFFER_SIZE];
	long amount;

	while ((amount = u_read(descriptor, buffer, sizeof(buffer))) > 0) {
		size_t offset = 0;
		while (offset < (size_t)amount) {
			long written = u_write(1, buffer + offset,
			    (size_t)amount - offset);
			if (written <= 0)
				return 0;
			offset += (size_t)written;
		}
	}
	return amount == 0;
}

int
user_main(uint64_t argc, char **argv)
{
	uint64_t index;
	int failed = 0;

	if (argc == 1)
		return copy_descriptor(0) ? 0 : 1;
	for (index = 1; index < argc; index++) {
		char path[PATH_SIZE];
		long descriptor;
		if (!u_normalize_path("/", argv[index], path, sizeof(path))) {
			u_puts("cat: path too long: ");
			u_puts(argv[index]);
			u_puts("\n");
			failed = 1;
			continue;
		}
		descriptor = u_open(path, U_O_RDONLY);
		if (descriptor < 0) {
			u_puts("cat: cannot open: ");
			u_puts(argv[index]);
			u_puts("\n");
			failed = 1;
			continue;
		}
		if (!copy_descriptor((uint64_t)descriptor)) {
			u_puts("cat: read/write failed: ");
			u_puts(argv[index]);
			u_puts("\n");
			failed = 1;
		}
		(void)u_close((uint64_t)descriptor);
	}
	return failed;
}
