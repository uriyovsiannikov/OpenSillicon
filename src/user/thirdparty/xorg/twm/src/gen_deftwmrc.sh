#!/bin/sh

rm -f ${1}
echo '/*' >>${1}
echo ' * This file is generated automatically from the default' >>${1}
echo ' * twm bindings file system.twmrc by the twm Makefile.' >>${1}
echo ' */' >>${1}
echo '' >>${1}
echo 'const unsigned char defTwmrc[] = {' >>${1}
sed \
    -e 's/^#.*$//g' \
    -e 's/"/\\"/g' \
    -e 's/^ *\(.*\) *$/    "\1\\n"/g' \
    <${2} \
    >>${1}
printf '%s\n' '"\n"};' >>${1}
echo 'const unsigned long defTwmrc_len = sizeof(defTwmrc);' >>${1}
