#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
int nano_host_errno;

char *os_basename(char *);
char *os_dirname(char *);
char *os_strpbrk(const char *, const char *);
char *os_strcasestr(const char *, const char *);

int main(void)
{
    char path1[] = "/tmp/file.txt///", path2[] = "/tmp///file.txt/";
    char root[] = "////", single[] = "file", empty[] = "";
    assert(strcmp(os_basename(path1), "file.txt") == 0);
    assert(strcmp(os_dirname(path2), "/tmp") == 0);
    assert(strcmp(os_basename(root), "/") == 0);
    assert(strcmp(os_dirname(root), "/") == 0);
    assert(strcmp(os_dirname(single), ".") == 0);
    assert(strcmp(os_basename(empty), ".") == 0);
    assert(strcmp(os_dirname(NULL), ".") == 0);
    const char *text = "Abc file.TXT";
    assert(os_strpbrk(text, ". ") == text + 3);
    assert(os_strpbrk(text, "z") == NULL);
    assert(os_strcasestr(text, "FILE.txt") == text + 4);
    assert(os_strcasestr(text, "") == text);
    assert(os_strcasestr(text, "long missing text") == NULL);
    puts("nano libos path and search helpers passed");
    return 0;
}
