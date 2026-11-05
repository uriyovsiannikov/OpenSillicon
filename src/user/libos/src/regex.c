#include <regex.h>
#include <errno.h>
#include <string.h>

int regcomp(regex_t *expression,const char *pattern,int flags)
{(void)pattern;(void)flags;if(expression){expression->unsupported=1;expression->re_nsub=0;}errno=EOPNOTSUPP;return REG_BADPAT;}
int regexec(const regex_t *expression,const char *text,size_t count,regmatch_t *matches,int flags)
{(void)expression;(void)text;(void)count;(void)matches;(void)flags;return REG_NOMATCH;}
void regfree(regex_t *expression){if(expression)expression->unsupported=0;}

size_t regerror(int error, const regex_t *expression, char *buffer, size_t size)
{
	const char *message = error == REG_NOMATCH ? "No match" :
	    "Regular expressions are not supported";
	size_t needed = strlen(message) + 1;
	(void)expression;
	if (buffer != NULL && size != 0) {
		size_t copied = needed < size ? needed : size;
		memcpy(buffer, message, copied - 1);
		buffer[copied - 1] = '\0';
	}
	return needed;
}
