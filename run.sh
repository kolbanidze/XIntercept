#!/bin/bash

FILE="master_key.txt"
PORT="5000"

if [ -f $FILE ]; then
    echo "Warning! $FILE exists! Renaming $FILE to $FILE.bak"
    mv $FILE $FILE".bak"
fi

pk="$(cat pk.txt)"
sk="$(cat sk.txt)"
./vxserver -p $PORT -k $sk -o $pk > $FILE
