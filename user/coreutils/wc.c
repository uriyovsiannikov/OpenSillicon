#include "../include/os.h"

#define BUFFER_SIZE 512U
#define PATH_SIZE 256U

struct counts {
	uint64_t lines;
	uint64_t words;
	uint64_t bytes;
};

static int
is_space(char value)
{
	return value == ' ' || value == '\t' || value == '\n' ||
	    value == '\r' || value == '\v' || value == '\f';
}

static int
count_descriptor(uint64_t descriptor, struct counts *counts)
{
	char buffer[BUFFER_SIZE];
	int in_word = 0;
	long amount;
	counts->lines = 0;
	counts->words = 0;
	counts->bytes = 0;
	while ((amount = u_read(descriptor, buffer, sizeof(buffer))) > 0) {
		size_t i;
		if (counts->bytes > UINT64_MAX - (uint64_t)amount)
			return 0;
		counts->bytes += (uint64_t)amount;
		for (i = 0; i < (size_t)amount; i++) {
			int whitespace = is_space(buffer[i]);
			if (buffer[i] == '\n') {
				if (counts->lines == UINT64_MAX)
					return 0;
				counts->lines++;
			}
			if (whitespace)
				in_word = 0;
			else if (!in_word) {
				if (counts->words == UINT64_MAX)
					return 0;
				counts->words++;
				in_word = 1;
			}
		}
	}
	return amount == 0;
}

static void
print_counts(const struct counts *counts, int lines, int words, int bytes,
	const char *name)
{
	int separated = 0;
	if (lines) {
		u_put_u64(counts->lines);
		separated = 1;
	}
	if (words) {
		if (separated)
			u_puts(" ");
		u_put_u64(counts->words);
		separated = 1;
	}
	if (bytes) {
		if (separated)
			u_puts(" ");
		u_put_u64(counts->bytes);
		separated = 1;
	}
	if (name != NULL) {
		if (separated)
			u_puts(" ");
		u_puts(name);
	}
	u_puts("\n");
}

static int
count_file(const char *name, struct counts *counts)
{
	long descriptor;
	int result;
	if (u_streq(name, "-"))
		return count_descriptor(0, counts);
	descriptor = u_open(name, U_O_RDONLY);
	if (descriptor < 0)
		return 0;
	result = count_descriptor((uint64_t)descriptor, counts);
	if (u_close((uint64_t)descriptor) < 0)
		result = 0;
	return result;
}

int
user_main(uint64_t argc, char **argv)
{
	int show_lines = 0;
	int show_words = 0;
	int show_bytes = 0;
	uint64_t first_file = 1;
	uint64_t i;
	int failed = 0;
	if (argc == 1) {
		show_lines = show_words = show_bytes = 1;
	} else {
		while (first_file < argc && argv[first_file][0] == '-' &&
		    argv[first_file][1] != '\0') {
			const char *option = argv[first_file] + 1;
			size_t j;
			if (u_streq(argv[first_file], "--")) {
				first_file++;
				break;
			}
			for (j = 0; option[j] != '\0'; j++) {
				if (option[j] == 'l') show_lines = 1;
				else if (option[j] == 'w') show_words = 1;
				else if (option[j] == 'c') show_bytes = 1;
				else {
					u_puts("usage: wc [-lwc] [file ...]\n");
					return 1;
				}
			}
			first_file++;
		}
		if (!show_lines && !show_words && !show_bytes)
			show_lines = show_words = show_bytes = 1;
	}
	if (first_file == argc) {
		struct counts counts;
		if (!count_descriptor(0, &counts))
			return 1;
		print_counts(&counts, show_lines, show_words, show_bytes, NULL);
		return 0;
	}
	for (i = first_file; i < argc; i++) {
		struct counts counts;
		if (!count_file(argv[i], &counts)) {
			u_puts("wc: cannot read: ");
			u_puts(argv[i]);
			u_puts("\n");
			failed = 1;
			continue;
		}
		print_counts(&counts, show_lines, show_words, show_bytes, argv[i]);
	}
	return failed;
}
