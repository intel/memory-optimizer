#!/bin/bash

commit=$(git rev-list -1 HEAD)

diff=$(git diff HEAD)
if [[ -n "$diff" ]]; then
    diff_sum=+$(echo "$diff" | md5sum | cut -f1 -d ' ')
    diff_stat=$(echo "$diff" | diffstat | sed 's/$/\\n\\/')
    diff_stat="\n\\"$'\n'"${diff_stat::-3}"
else
    diff_sum=
    diff_stat=
fi

cat > version.h <<EOF
#ifndef _VERSION_H_
#define _VERSION_H_

#define VERSION_TIME		"$(date +'%F %T')"
#define VERSION_COMMIT		"$commit"
#define VERSION_DIFFSUM		"$diff_sum"
#define VERSION_DIFFSTAT	"$diff_stat"

void print_version()
{
    printf("%s\n", VERSION_TIME " " VERSION_COMMIT VERSION_DIFFSUM VERSION_DIFFSTAT);
}

#endif
EOF
