#!/bin/bash

make clean
make
cp libscheduler.so checker-lin
cd checker-lin
make -f Makefile.checker
cd -