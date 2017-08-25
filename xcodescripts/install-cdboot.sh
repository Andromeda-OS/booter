#!/bin/sh

#  install-cdboot.sh
#  booter
#
#  Created by William Kent on 8/25/17.
#  

# SCRIPT_INPUT_FILE_0 = cdboot.s
nasm "${SCRIPT_INPUT_FILE_0}" -o "${DERIVED_FILE_DIR}/cdboot"

# SCRIPT_INPUT_FILE_1 = compiled, converted boot2
dd if="${SCRIPT_INPUT_FILE_1}" of="${DERIVED_FILE_DIR}/cdboot" conv=sync bs=2k seek=1

# SCRIPT_OUTPUT_FILE_0 = installation target
mkdir -p `dirname "${SCRIPT_OUTPUT_FILE_0}"`
cp "${DERIVED_FILE_DIR}/cdboot" "${SCRIPT_OUTPUT_FILE_0}"
