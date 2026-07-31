# Android Build Guide

This guide covers building the Source Engine for Android, including both the
original OpenGL ES backend (`togles`) and the new Vulkan backend (`toglesvk`).

## Prerequisites

### Required Tools

- **Android NDK r10e** (for ARMv7 builds with GCC 4.9)
  - Download: https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip
  - For ARM64 builds, NDK r19/r20 with Clang is recommended
- **Python 2.7+** (for waf build system)
- **Linux x86_64 host** (cross-compilation target)
- **Git** (for submodule management)

### Required Submodules

```bash
git submodule init && git submodule update
```

This fetches:
- `thirdparty/` — header-only dependencies (SDL2, FreeType, OpenAL, etc.)
- `lib/` — prebuilt static/shared libraries per architecture
- `ivp/` — physics (Havok/IVP)

### Android API Level Requirements

| Backend | Minimum API | Recommended API | Notes |
|---------|------------|-----------------|-------|
| ToGLES (OpenGL ES) | 21 (Android 5.0) | 21 | GLES 3.0 required |
| ToGLESVk (Vulkan) | 24 (Android 7.0) | 26+ | Vulkan 1.0+ required |

> **Note**: The Vulkan backend can *compile* against API 21 (it uses `dlopen`
> to load `libvulkan.so` at runtime), but will only *run* on API 24+ devices
> that have Vulkan drivers.

## Quick Start

### Option 1: Use Build Scripts

```bash
# GLES backend (original)
./scripts/build-android-armv7a.sh

# Vulkan backend
./scripts/build-android-vulkan.sh
```

### Option 2: Manual Configuration

```bash
# Set NDK path
export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/

# Configure for GLES backend
./waf configure -T debug \
    --android=armeabi-v7a-hard,4.9,21 \
    --togles \
    --disable-warns

# Configure for Vulkan backend
./waf configure -T debug \
    --android=armeabi-v7a-hard,4.9,24 \
    --use-vulkan \
    --disable-warns

# Build
./waf build
```

## Build Configuration Details

### The `--android` Flag

Format: `--android=<arch>,<toolchain>,<api>`

| Component | Values | Description |
|-----------|--------|-------------|
| `arch` | `armeabi-v7a-hard`, `armeabi-v7a`, `aarch64`, `x86`, `x86_64` | Target architecture |
| `toolchain` | `4.9` (GCC), `clang` | Compiler toolchain |
| `api` | `21`, `24`, `26`, `28`, `29`, `30`... | Android API level |

**ARMv7 hard-float** (`armeabi-v7a-hard`) is recommended for best performance.
It uses hardware floating-point (NEON/VFPv4) and is compatible with most
modern Android devices.

### Architecture-Specific Flags

For `armeabi-v7a-hard`:
```
-mfpu=neon-vfpv4 -mcpu=cortex-a7 -mtune=cortex-a7
-D_NDK_MATH_NO_SOFTFP=1 -mfloat-abi=hard
```

### Output Location

Build output goes to:
```
build/android/armeabi-v7a/lib/
```

Key output files:
- `libhl2_launcher.so` — main engine shared library
- `libtogl.so` — rendering backend (GLES or Vulkan, depending on config)
- `libengine.so`, `libmaterialsystem.so`, etc. — engine modules

## Creating an APK

This repository only builds **native `.so` libraries**. To create a complete
APK, you need a separate Android wrapper project.

### Step 1: Create Android Project

Create a new Android Studio project with:
- `minSdkVersion` = 24 (for Vulkan) or 21 (for GLES)
- `com.valvesoftware.ValveActivity2` as the main Activity class

### Step 2: Copy Native Libraries

```bash
# Copy .so files to jniLibs
mkdir -p app/src/main/jniLibs/armeabi-v7a/
cp build/android/armeabi-v7a/lib/*.so app/src/main/jniLibs/armeabi-v7a/
```

### Step 3: Create Activity

```java
package com.valvesoftware;

import org.libsdl.app.SDLActivity;

public class ValveActivity2 extends SDLActivity {
    // Load native libraries
    static {
        System.loadLibrary("hl2_launcher");
    }

    public native void setArgs(String[] args);
    public native void nativeOnActivityResult(int requestCode, int resultCode);
    public native static void setenv(String key, String value);
}
```

### Step 4: Package Game Data

Copy your game's `hl2/` directory (or mod directory) into `assets/` or
`sdcard/Android/data/<package>/files/`.

### Step 5: Build APK

```bash
./gradlew assembleDebug
```

## Supported Android Architectures

| Architecture | NDK | Status | Notes |
|-------------|-----|--------|-------|
| `armeabi-v7a-hard` | r10e | Primary target | 32-bit ARM, hard-float |
| `aarch64` | r19+ | Experimental | 64-bit ARM, requires Clang |
| `x86` | r19+ | Community | For emulators |
| `x86_64` | r19+ | Community | For emulators |

## Vulkan Device Compatibility

Not all Android devices support Vulkan, even on API 24+. Compatibility depends
on the GPU vendor and driver:

| GPU Vendor | Vulkan Support | Notes |
|-----------|----------------|-------|
| Qualcomm (Adreno) | 5xx+: Full | Most common Android GPU |
| ARM (Mali) | Midgard+: Full | Bifrost/Valhall recommended |
| Imagination (PowerVR) | Rogue+: Partial | Driver quality varies |
| ARM Mali-4xx | None | Pre-Vulkan GPU |

You can check Vulkan support at runtime:
```java
// In your Activity
if (getPackageManager().hasSystemFeature("android.hardware.vulkan.level")) {
    // Vulkan supported
}
```

## Troubleshooting

### "dlopen: libvulkan.so not found"

The device doesn't have Vulkan support. Either:
- Use the GLES backend (`--togles`) instead
- Test on a device with Android 7.0+ and a Vulkan-capable GPU

### "NDK not found"

Ensure `ANDROID_NDK_HOME` points to the extracted NDK:
```bash
export ANDROID_NDK_HOME=/absolute/path/to/android-ndk-r10e
```

### Build fails on submodule initialization

The `thirdparty/` and `lib/` submodules point to separate repositories:
- `https://github.com/nillerusr/source-thirdparty`
- `https://github.com/nillerusr/source-engine-libs`

Ensure network access and try:
```bash
git submodule update --init --recursive --force
```

### Crash on startup

Common causes:
- Missing game data files in the correct path
- Architecture mismatch (e.g., running ARMv7 libs on ARM64-only device)
- Insufficient RAM (Source Engine needs ~1GB for HL2-level content)
- Missing SDL2 library (`libSDL2.so` must be in the same directory as engine libs)
