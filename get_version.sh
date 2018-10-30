#!/bin/bash

VERSION=$(git log -n 1 | grep ^commit)
CHANGE_STATE=$(git diff --numstat)$(git diff --cached --numstat)
TARGET_FILE=version.h

if [[ "$CHANGE_STATE" != "" ]]; then
    DIRTY_FLAG="+local_changes"
    UNSTAGED_DIGEST=$(git diff | md5sum)
    STAGED_DIGEST=$(git diff --cached | md5sum)
else
    DIRTY_FLAG=""
    UNSTAGED_DIGEST=""
    STAGED_DIGEST=""
fi

echo "#ifndef _VERSION_H_"  > $TARGET_FILE
echo "#define _VERSION_H_"  >> $TARGET_FILE

echo ""  >> $TARGET_FILE
echo "#define SOURCE_CODE_DIRTY \"$DIRTY_FLAG\""  >> $TARGET_FILE
echo "#define VERSION_STRING \"$VERSION\""  >> $TARGET_FILE
echo "#define UNSTAGED_DIGEST \"$UNSTAGED_DIGEST\""  >> $TARGET_FILE
echo "#define STAGED_DIGEST \"$STAGED_DIGEST\""  >> $TARGET_FILE

echo ""  >> $TARGET_FILE
echo "static const char unstaged_change[] = {" >> $TARGET_FILE
echo "#include \"unstaged_patch.cstr\"" >> $TARGET_FILE
echo "};" >> $TARGET_FILE

echo "" >> $TARGET_FILE
echo "static const char staged_change[] = {" >> $TARGET_FILE
echo "#include \"staged_patch.cstr\"" >> $TARGET_FILE
echo "};" >> $TARGET_FILE
echo "" >> $TARGET_FILE

echo "#endif"  >> $TARGET_FILE
