#!/bin/bash
#
# Copyright (C) 2025 Rodrigo Arias Mallo <rodarima@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

set -eux

i="$SRCDIR/test.html"
o="$WORKDIR/output.html"

# Split the input into two writes and verify that it is the same page
(head -3 "$i"; tail +4 "$i") | $DILLOC load
$DILLOC dump > "$o"

diff -up "$i" "$o"
