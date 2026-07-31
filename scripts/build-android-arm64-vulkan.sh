#!/bin/sh

# Build script for Android arm64-v8a (AArch64) with Vulkan backend
# Uses NDK r20 with Clang toolchain
#
# Requirements:
#   - Android NDK r20 (or r19)
#   - 64-bit Linux x86_64 host
#   - Target: Android 7.0+ (API 24) with Vulkan-capable GPU

set -e

echo "=== Source Engine Android arm64-v8a Vulkan Build ==="

# Initialize submodules
echo ">>> Initializing submodules..."
git submodule init && git submodule update

# Download NDK r20 if not present
if [ ! -d "android-ndk-r20" ]; then
	echo ">>> Downloading Android NDK r20..."
	wget -q https://dl.google.com/android/repository/android-ndk-r20-linux-x86_64.zip -o /dev/null
	unzip -q android-ndk-r20-linux-x86_64.zip
fi

export ANDROID_NDK_HOME=$PWD/android-ndk-r20/
export NDK_HOME=$PWD/android-ndk-r20/

# aarch64 + Vulkan requires:
#   - NDK r19+ (Clang only)
#   - API 24+ (Android 7.0 for Vulkan support)
echo ">>> Configuring build (arm64-v8a, Vulkan backend)..."
./waf configure -T debug \
	--android=aarch64,clang,24 \
	--use-vulkan \
	--disable-warns

echo ">>> Building..."
./waf build

echo "=== Build complete! ==="
echo "Native libraries are in: build/android/arm64-v8a/lib/"
echo ""
echo "To create an APK:"
echo "  1. Copy the .so files to your Android project's jniLibs/arm64-v8a/ directory"
echo "  2. Ensure the target device runs Android 7.0+ (API 24+) with Vulkan support"
echo "  3. Build the APK with Gradle from the Android wrapper project"
