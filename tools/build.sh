#!/bin/bash
export ARCH=aarch64
export CROSS_COMPILE=aarch64-linux-gnu-
export KERNEL_DEV_PATH=~/Workspace/l4t/sources/kernel/kernel-4.9
export TOOLCHAIN_PREFIX=aarch64-linux-gnu-
make $1
