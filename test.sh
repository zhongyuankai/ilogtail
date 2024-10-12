#!/bin/bash
set -xue
set -o pipefail

# unittest_plugin
make unittest_plugin

# unittest_core
make clean
mkdir -p core/build && cd core/build && cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_LOGTAIL_UT=ON -DENABLE_COMPATIBLE_MODE=OFF -DENABLE_STATIC_LINK_CRT=OFF -DWITHOUTGDB==OFF .. && make -sj 12 && cd ../../
make unittest_core