#!/bin/bash

VERSION=$(git log -n 1 | grep ^commit)
CHANGE_STATE=$(git status -s | more | grep -e " M " -e " D " -e " R " -e " C " -e " U ")
TARGET_FILE=version.h

if [[ "$CHANGE_STATE" != "" ]]; then
    DIRTY_FLAG="+local_changes"
else
    DIRTY_FLAG=""
fi

echo $VERSION

echo "#ifndef _VERSION_H_"  > $TARGET_FILE
echo "#define _VERSION_H_"  >> $TARGET_FILE
echo ""  >> $TARGET_FILE
echo "#define VERSION_STRING \"$VERSION $DIRTY_FLAG\""  >> $TARGET_FILE
echo ""  >> $TARGET_FILE
echo "#endif"  >> $TARGET_FILE
