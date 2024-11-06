#!/bin/bash
set -xue
set -o pipefail

OUT_DIR=${1:-output}
DIST_DIR=${2:-dist}
PACKAGE_DIR=${3:-ilogtail-2.0.7}
ROOTDIR=$(cd $(dirname "${BASH_SOURCE[0]}") && cd .. && pwd)

# 1.进行编译
# rm -fr core/build

continue=0

if [ "$1" = "-c" ];
then
  continue=1
fi

if [ "$continue" != 1 ];
then
  read -p "Are you sure to remove contrib and build directory?(y/n): " input
  echo "inptu: " $input
  if [ "$input" != 'y' ];
  then
      echo "build canceled!"
      exit 1
  fi

  rm -rf ./core/build
  mkdir ./core/build && cd ./core/build
  cmake .. -DCMAKE_BUILD_TYPE=Release -DLOGTAIL_VERSION=2.0.7 -DBUILD_LOGTAIL_UT=OFF -DENABLE_COMPATIBLE_MODE=OFF -DENABLE_STATIC_LINK_CRT=ON -DWITHOUTGDB==OFF
else
  cd ./core/build
fi

make -sj 18

# 编译成功
echo -e "build done."

mkdir -p "${ROOTDIR}/${OUT_DIR}"
cp "${ROOTDIR}/core/build/ilogtail" "${ROOTDIR}/${OUT_DIR}"
cp "${ROOTDIR}/ilogtail_config.json" "${ROOTDIR}/${OUT_DIR}"
cp "${ROOTDIR}/control" "${ROOTDIR}/${OUT_DIR}"

# prepare dist dir
mkdir -p "${ROOTDIR}/${DIST_DIR}/${PACKAGE_DIR}"
cp "${ROOTDIR}/${OUT_DIR}/ilogtail" "${ROOTDIR}/${DIST_DIR}/${PACKAGE_DIR}"
cp "${ROOTDIR}/${OUT_DIR}/ilogtail_config.json" "${ROOTDIR}/${DIST_DIR}/${PACKAGE_DIR}"
cp "${ROOTDIR}/${OUT_DIR}/control" "${ROOTDIR}/${DIST_DIR}/${PACKAGE_DIR}"

strip "${ROOTDIR}/${DIST_DIR}/${PACKAGE_DIR}/ilogtail"

# pack dist dir
cd "${ROOTDIR}/${DIST_DIR}"
tar -cvzf "${PACKAGE_DIR}.tar.gz" "${PACKAGE_DIR}"
rm -rf "${PACKAGE_DIR}"
cd "${ROOTDIR}"
