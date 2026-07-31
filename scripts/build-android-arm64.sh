#!/bin/sh

# Build script for Android arm64-v8a (AArch64, 64-bit ARM)
# Uses NDK r20 with Clang toolchain (GCC removed from NDK r18+)
#
# Requirements:
#   - Android NDK r20 (or r19)
#   - 64-bit Linux x86_64 host

set -e

echo "=== Source Engine Android arm64-v8a Build ==="

# Initialize submodules
echo ">>> Initializing submodules..."
git submodule init && git submodule update

# Download NDK r20 if not present
# Note: r10e does NOT support aarch64; r19/r20 required
if [ ! -d "android-ndk-r20" ]; then
	echo ">>> Downloading Android NDK r20..."
	wget -q https://dl.google.com/android/repository/android-ndk-r20-linux-x86_64.zip -o /dev/null
	unzip -q android-ndk-r20-linux-x86_64.zip
fi

export ANDROID_NDK_HOME=$PWD/android-ndk-r20/
export NDK_HOME=$PWD/android-ndk-r20/

# aarch64 requires:
#   - NDK r19+ (Clang only, no GCC)
#   - API >= 21 (64-bit targets need min API 21)
#   - toolchain: clang
echo ">>> Configuring build (arm64-v8a, GLES backend)..."
./waf configure -T debug \
	--android=aarch64,clang,21 \
	--togles \
	--disable-warns

echo ">>> Building..."
./waf build

echo "=== Build complete! ==="
echo "Native libraries are in: build/android/arm64-v8a/lib/"
