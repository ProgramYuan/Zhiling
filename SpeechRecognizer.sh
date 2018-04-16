#!/bin/bash

order=$1
if [ ! -n "$order" ]; then
        echo "order can not be null"
else
export LD_LIBRARY_PATH=$(pwd)
./SpeechRecognizer ${order}
fi