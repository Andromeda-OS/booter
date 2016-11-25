#!/bin/sh

#  copy-libsa-headers.sh
#  booter
#
#  Created by William Kent on 11/24/16.
#

set -e
DIR="${XNU_INSTALL_ROOT}/System/Library/Frameworks/System.framework/Versions/Current/PrivateHeaders/standalone"

mkdir -p $DIR
cp "${SRCROOT}/libsa/libsa.h" "${DIR}/libsa.h"
