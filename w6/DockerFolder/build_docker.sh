#!/bin/bash

cd ~/src/workshop6-c/src
mkdir -p cmake/build
pushd cmake/build
rm -rf *
cmake -DCMAKE_PREFIX_PATH=~/src ~/src/workshop6-c ~/src/workshop6-c/src
make -j