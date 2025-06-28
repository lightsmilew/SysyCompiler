#!/bin/bash
if [ "$1" == "-rebuild" ]; then
    rm -rf myCompiler/build
    mkdir -p myCompiler/build
    cd myCompiler/build
    cmake ..
    make
    cd ../..
    python3 test.py
elif [ "$1" == "-build" ]; then
    cd myCompiler/build
    make
    cd ../..
    python3 test.py
else
    python3 test.py
fi