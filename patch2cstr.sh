#!/bin/bash

export IFS=
$* | sed 's/\\/\\\\/g' | while read line; do
    LINE=$(echo "$line" | sed 's/\\/\\\\/g' | sed 's/\"/\\\"/g')
    echo \"$LINE\\n\"
    done;
