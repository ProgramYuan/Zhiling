#!/bin/bash

gramid=$1
if [ ! -n "$gramid" ]; then
        echo "Grammar ID can not be null"
else
export LD_LIBRARY_PATH=$(pwd)
./BuildGrammar ${gramid}
fi
