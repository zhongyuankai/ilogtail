#!/bin/bash
set -xue
set -o pipefail

# 1.进行编译
make dist

# 编译成功
echo -e "build done."
