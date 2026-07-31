#!/bin/sh

git submodule init && git submodule update

# NDK r10e 的 GCC 4.9 工具链是 32 位 ELF，ubuntu-24.04 需要装 32 位运行时库
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install -y lib32z1 lib32stdc++6

wget https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip -o /dev/null
unzip android-ndk-r10e-linux-x86_64.zip
export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
export NDK_HOME=$PWD/android-ndk-r10e/
./waf configure -T debug --android=armeabi-v7a-hard,4.9,21 --togles --disable-warns &&
./waf build
