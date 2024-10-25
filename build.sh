#!/bin/bash
set -xue
set -o pipefail

# go版本1.19
# 添加go modules相关环境变量
export GOPROXY=https://goproxy.cn,direct
export GO111MODULE=on

# 1.进行编译
make dist

# 编译成功
echo -e "build done."
