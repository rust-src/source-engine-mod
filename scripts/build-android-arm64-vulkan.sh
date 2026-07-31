#!/bin/sh

# Build script for Android arm64-v8a with Vulkan backend (toglesvk)
# Uses NDK r10e sysroot + standalone LLVM/Clang 11.1.0 (host toolchain)
# Requires API 24+ at runtime for libvulkan.so

git submodule init && git submodule update

sudo apt-get update
sudo apt-get install -f -y libopenal-dev g++-multilib gcc-multilib libpng-dev libjpeg-dev libfreetype6-dev libfontconfig1-dev libcurl4-gnutls-dev libsdl2-dev zlib1g-dev libbz2-dev libedit-dev

wget https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip -nc -q
wget https://github.com/llvm/llvm-project/releases/download/llvmorg-11.1.0/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz -nc -q

unzip -q android-ndk-r10e-linux-x86_64.zip
tar -xf clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz

export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
export PATH=$PWD/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04/bin:$PATH

# NDK r10e 没有 Vulkan 头文件 (Vulkan headers 从 NDK r14 才加入)
# 下载 Khronos 官方 Vulkan 头文件
if [ ! -d "vulkan-headers/include/vulkan" ]; then
	wget -q https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.2.182.tar.gz -O vulkan-headers.tar.gz
	tar -xzf vulkan-headers.tar.gz
	mv Vulkan-Headers-1.2.182 vulkan-headers
fi

python3 ./waf configure -T release --prefix=../android_build --android=aarch64,host,24 --target=../android_build/aarch64-vulkan --disable-warns --use-vulkan
python3 ./waf install --strip
