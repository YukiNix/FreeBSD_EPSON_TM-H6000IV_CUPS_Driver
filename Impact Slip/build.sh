#!/bin/sh

#build in directory
if [ -d build ]
then
  rm -R build
fi
mkdir build
cd build
cmake ..
make
