/* Minimal fallbacks for feh features that need unavailable OS services. */
#include "feh.h"
#include "gib_list.h"

#include <string.h>

gib_list *feh_wrap_string(char *text, int wrap_width, Imlib_Font font,
		gib_style *style)
{
	(void)wrap_width;
	(void)font;
	(void)style;
	return text ? gib_list_add_front(NULL, estrdup(text)) : NULL;
}

char *build_caption_filename(feh_file *file, short create_dir)
{
	(void)file;
	(void)create_dir;
	return NULL;
}
