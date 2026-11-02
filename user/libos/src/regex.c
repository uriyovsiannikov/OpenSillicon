#include <regex.h>
#include <errno.h>

int regcomp(regex_t *expression,const char *pattern,int flags)
{(void)pattern;(void)flags;if(expression)expression->unsupported=1;errno=EOPNOTSUPP;return REG_BADPAT;}
int regexec(const regex_t *expression,const char *text,size_t count,regmatch_t *matches,int flags)
{(void)expression;(void)text;(void)count;(void)matches;(void)flags;return REG_NOMATCH;}
void regfree(regex_t *expression){if(expression)expression->unsupported=0;}
