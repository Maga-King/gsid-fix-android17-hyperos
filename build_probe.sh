#!/bin/sh
set -eu
NDK_ROOT=${ANDROID_NDK_HOME:-$HOME/android-ndk-r29}
NDK="$NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin"
mkdir -p build
"$NDK/clang++" --target=aarch64-linux-android35 -O2 -fno-exceptions -fno-rtti -nostdlib++ -Wall -Wextra -Werror native/image_probe.cpp evidence/libbinder_ndk.so -o build/image_probe
