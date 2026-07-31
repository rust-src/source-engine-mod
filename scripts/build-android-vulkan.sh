#!/bin/sh

# Build script for Android with Vulkan backend
# This builds the source engine with the DX9-to-Vulkan translation layer (toglesvk)
# instead of the DX9-to-GLES layer (togles)
#
# Requirements:
#   - Android NDK r10e (or newer for arm64)
#   - Vulkan headers (included in NDK r14+ for API 24+)
#   - libvulkan.so (available on Android 7.0+ / API 24+)
#
# For API 21 (Android 5.0), libvulkan.so may not be available.
# The toglesvk module uses dlopen("libvulkan.so") at runtime,
# so it can still compile against API 21 but requires API 24+ to run.

set -e

echo "=== Source Engine Android Vulkan Build ==="

# Initialize submodules
echo ">>> Initializing submodules..."
git submodule init && git submodule update

# NDK r10e 的 GCC 4.9 工具链是 32 位 ELF，ubuntu-24.04 需要装 32 位运行时库
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install -y lib32z1 lib32stdc++6

# Download NDK if not present
if [ ! -d "android-ndk-r10e" ]; then
	echo ">>> Downloading Android NDK r10e..."
	wget -q https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip -o /dev/null
	unzip -q android-ndk-r10e-linux-x86_64.zip
fi

export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
export NDK_HOME=$PWD/android-ndk-r10e/

# NDK r10e 没有 Vulkan 头文件 (Vulkan headers 从 NDK r14 才加入)
# 下载 Khronos 官方 Vulkan 头文件
if [ ! -d "vulkan-headers/include/vulkan" ]; then
	echo ">>> Downloading Vulkan headers..."
	wget -q https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.2.182.tar.gz -O vulkan-headers.tar.gz
	tar -xzf vulkan-headers.tar.gz
	mv Vulkan-Headers-1.2.182 vulkan-headers
fi

# Build for armv7a with hard-float.
# NDK r10e only ships platform headers up to API 21, so we compile against
# API 21 + the downloaded Khronos Vulkan headers.  libvulkan.so is dlopen()'d
# at runtime, so the binary still requires API 24+ (Android 7.0) to run.
echo ">>> Configuring build (armv7a, Vulkan backend)..."
./waf configure -T debug \
	--android=armeabi-v7a-hard,4.9,21 \
	--use-vulkan \
	--disable-warns

echo ">>> Building..."
./waf build

echo "=== Build complete! ==="
echo "Native libraries are in: build/android/armeabi-v7a/lib/"
echo ""
echo "To create an APK:"
echo "  1. Copy the .so files to your Android project's jniLibs/armeabi-v7a/ directory"
echo "  2. Ensure the target device runs Android 7.0+ (API 24+) with Vulkan support"
echo "  3. Build the APK with Gradle from the Android wrapper project"
