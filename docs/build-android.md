# Android Build Guide

This guide covers building the Source Engine for Android, including both the
original OpenGL ES backend (`togles`) and the new Vulkan backend (`toglesvk`).

## Prerequisites

### Required Tools

- **Android NDK r10e** (provides sysroot for all Android targets, including arm64)
  - Download: https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip
  - Note: r10e does *not* ship a bundled aarch64 compiler; for arm64-v8a you
    must supply a standalone Clang (see below) and use the `host` toolchain.
- **LLVM/Clang 11.1.0** (required for arm64-v8a `host` toolchain builds)
  - Download: https://github.com/llvm/llvm-project/releases/download/llvmorg-11.1.0/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz
  - armv7-a builds use the GCC 4.9 toolchain bundled in NDK r10e instead.
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

### NDK Requirements by Architecture

| Architecture | NDK Version | Toolchain | Compiler | Notes |
|-------------|-------------|-----------|----------|-------|
| `armeabi-v7a-hard` (32-bit ARM) | r10e | `4.9` | GCC 4.9 (bundled) | Hard-float ABI, NEON |
| `aarch64` (64-bit ARM) | r10e | `host` | Clang 11.1.0 (standalone) | r10e provides sysroot only |

> **Note**: arm64-v8a (aarch64) is built using the **`host` toolchain** mode of
> `xcompile.py`: r10e supplies the sysroot (`platforms/android-21/arch-arm64`)
> and a standalone LLVM/Clang 11.1.0 performs the actual cross-compilation via
> `clang --target=aarch64-linux-android21`. NDK r19/r20 with the bundled `clang`
> toolchain is an alternative, but the canonical scripts in this repo use r10e +
> standalone Clang for consistency with the armv7-a flow.

## Quick Start

### Option 1: Use Build Scripts

```bash
# --- armv7-a (32-bit) ---
# GLES backend
./scripts/build-android-armv7a.sh

# Vulkan backend
./scripts/build-android-vulkan.sh

# --- arm64-v8a (64-bit) ---
# GLES backend
./scripts/build-android-arm64.sh

# Vulkan backend
./scripts/build-android-arm64-vulkan.sh
```

### Option 2: Manual Configuration

#### armv7-a (32-bit ARM, NDK r10e)

```bash
export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/

# GLES backend
./waf configure -T debug --android=armeabi-v7a-hard,4.9,21 --togles --disable-warns
./waf build

# Vulkan backend
./waf configure -T debug --android=armeabi-v7a-hard,4.9,24 --use-vulkan --disable-warns
./waf build
```

#### arm64-v8a (64-bit ARM, NDK r10e + standalone Clang)

```bash
# Set NDK path and put standalone Clang on PATH
export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
export PATH=$PWD/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04/bin:$PATH

# GLES backend  (API 21) — build + install to ../android_build/aarch64
python3 ./waf configure -T release --prefix=../android_build \
    --android=aarch64,host,21 --target=../android_build/aarch64 \
    --disable-warns --togles
python3 ./waf install --strip

# Vulkan backend (API 24 — required for libvulkan.so at runtime)
python3 ./waf configure -T release --prefix=../android_build \
    --android=aarch64,host,24 --target=../android_build/aarch64-vulkan \
    --disable-warns --use-vulkan
python3 ./waf install --strip
```

## Build Configuration Details

### The `--android` Flag

Format: `--android=<arch>,<toolchain>,<api>`

| Component | Values | Description |
|-----------|--------|-------------|
| `arch` | `armeabi-v7a-hard`, `armeabi-v7a`, `aarch64`, `x86`, `x86_64` | Target architecture |
| `toolchain` | `4.9` (GCC), `clang`, `host` | Compiler toolchain |
| `api` | `21`, `24`, `26`, `28`, `29`, `30`... | Android API level |

> `host` means: use a standalone Clang found on `PATH` and drive it with
> `--target=<triple><api>`. This is how arm64-v8a is built against NDK r10e,
> which has no bundled aarch64 compiler.

**Valid combinations:**

| `arch` | `toolchain` | NDK | Compiler | Notes |
|--------|-------------|-----|----------|-------|
| `armeabi-v7a-hard` | `4.9` | r10e | GCC 4.9 (bundled) | 32-bit ARM, hard-float, NEON |
| `aarch64` | `host` | r10e | Clang 11.1.0 (standalone) | 64-bit ARM, r10e supplies sysroot |
| `aarch64` | `clang` | r19/r20 | Clang (bundled) | Alternative 64-bit ARM flow |

> arm64-v8a requires API >= 21 (enforced automatically by xcompile.py).

### Architecture-Specific Flags

For `armeabi-v7a-hard` (32-bit):
```
-mfpu=neon-vfpv4 -mcpu=cortex-a7 -mtune=cortex-a7
-D_NDK_MATH_NO_SOFTFP=1 -mfloat-abi=hard
```

For `aarch64` (64-bit, `host` toolchain):
- No special CPU flags needed (ARMv8-A baseline includes NEON/VFP)
- Driven by `clang --target=aarch64-linux-android<api>` from standalone LLVM
- r10e sysroot: `platforms/android-<api>/arch-arm64`
- STL path: `gnu-libstdc++/4.9/libs/arm64-v8a/`

### Output Location

| Architecture | Output Path |
|-------------|-------------|
| armv7-a (32-bit) | `build/android/armeabi-v7a/lib/` |
| arm64-v8a (64-bit, GLES) | `../android_build/aarch64/` |
| arm64-v8a (64-bit, Vulkan) | `../android_build/aarch64-vulkan/` |

> The arm64 scripts use `--prefix=../android_build --target=...` with
> `waf install --strip`, so the stripped `.so` files are installed into the
> directory given by `--target` rather than the default `build/` tree. The
> armv7-a scripts use the default `./waf build` flow, whose output stays under
> `build/android/`.

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
# For armv7-a (32-bit)
mkdir -p app/src/main/jniLibs/armeabi-v7a/
cp build/android/armeabi-v7a/lib/*.so app/src/main/jniLibs/armeabi-v7a/

# For arm64-v8a (64-bit) — output of build-android-arm64.sh / -vulkan.sh
mkdir -p app/src/main/jniLibs/arm64-v8a/
cp ../android_build/aarch64/*.so app/src/main/jniLibs/arm64-v8a/
# (or ../android_build/aarch64-vulkan/*.so for the Vulkan build)

# For both (multi-arch APK, recommended for distribution)
mkdir -p app/src/main/jniLibs/armeabi-v7a/ app/src/main/jniLibs/arm64-v8a/
cp build/android/armeabi-v7a/lib/*.so app/src/main/jniLibs/armeabi-v7a/
cp ../android_build/aarch64/*.so app/src/main/jniLibs/arm64-v8a/
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

| Architecture | NDK | Toolchain | Status | Notes |
|-------------|-----|-----------|--------|-------|
| `armeabi-v7a-hard` | r10e | `4.9` (GCC) | Primary target | 32-bit ARM, hard-float, NEON |
| `aarch64` | r10e | `host` (Clang 11.1.0) | Supported | 64-bit ARM (arm64-v8a) |
| `x86` | r19+ | `clang` | Community | For emulators |
| `x86_64` | r19+ | `clang` | Community | For emulators |

> **arm64-v8a (aarch64) is recommended for modern Android devices.**
> Most phones shipped since 2018 use 64-bit ARM. arm64 provides:
> - Full 64-bit addressing (larger address space for game assets)
> - Better register file (31 general-purpose registers vs 14 usable in ARMv7)
> - Improved AES/SHA crypto instructions (ARMv8.0-A)
> - No 32-bit NDK limitations (Google deprecated 64-bit-unaware apps in 2019)

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

### "NDK not found" / "Unknown NDK revision"

Ensure `ANDROID_NDK_HOME` points to the extracted NDK. Both armv7-a and arm64
builds in this repo use **NDK r10e**:
```bash
export ANDROID_NDK_HOME=/absolute/path/to/android-ndk-r10e
```

`xcompile.py` only accepts NDK revisions `10`, `19`, `20`
(`ANDROID_NDK_SUPPORTED`). Any other revision will fatal with
"Unknown NDK revision".

### arm64 build fails with "command not found: clang"

The `host` toolchain expects a standalone Clang on `PATH`. Download and extract
LLVM 11.1.0, then prepend its `bin/` to `PATH`:
```bash
wget https://github.com/llvm/llvm-project/releases/download/llvmorg-11.1.0/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz
tar -xf clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz
export PATH=$PWD/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04/bin:$PATH
```
`xcompile.py` then invokes `clang --target=aarch64-linux-android<api>` and uses
the r10e sysroot at `platforms/android-<api>/arch-arm64`.

### "aarch64" with wrong toolchain

For arm64-v8a against NDK r10e, the toolchain must be `host` (not `4.9`):
```bash
# Correct (r10e + standalone Clang)
--android=aarch64,host,21

# Wrong (r10e has no bundled aarch64 GCC)
--android=aarch64,4.9,21
```
If you are instead using NDK r19/r20, the bundled `clang` toolchain works too:
`--android=aarch64,clang,21`.

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
