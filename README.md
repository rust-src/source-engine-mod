# Source Engine

[![GitHub Actions Status](https://github.com/nillerusr/source-engine/actions/workflows/build.yml/badge.svg)](https://github.com/nillerusr/source-engine/actions/workflows/build.yml) [![GitHub Actions Status](https://github.com/nillerusr/source-engine/actions/workflows/tests.yml/badge.svg)](https://github.com/nillerusr/source-engine/actions/workflows/tests.yml)
 Discord: [![Discord Server](https://img.shields.io/discord/672055862608658432.svg)](https://discord.gg/hZRB7WMgGw)

Information from [wikipedia](https://wikipedia.org/wiki/Source_(game_engine)):

Source is a 3D game engine developed by Valve.
It debuted as the successor to GoldSrc with Half-Life: Source in June 2004,
followed by Counter-Strike: Source and Half-Life 2 later that year.
Source does not have a concise version numbering scheme; instead, it was released in incremental versions

Source code is based on TF2 2018 leak. Don't use it for commercial purposes.

This project is using waf buildsystem. If you have waf-related questions look https://waf.io/book

---

## Features

- Android, OSX, FreeBSD, Windows, Linux (glibc, musl) support
- ARM support (except Windows)
- 64-bit support
- Modern toolchains support
- Fixed many undefined behaviours
- Touch support (even on Windows/Linux/OSX)
- VTF 7.5 support
- PBR support
- BSP v19-v21 support (BSP v21 support is partial, Portal 2 and CSGO maps work fine)
- MDL v46-v49 support
- Removed useless/unnecessary dependencies
- Achievement system working without Steam
- Server browser works without Steam
- **Vulkan rendering backend** (`toglesvk`) for Android and Linux — see below

## Rendering Backends

This fork supports three rendering backends, selected at build time:

| Backend | Module | Target API | Platforms | Build Flag |
|---------|--------|-----------|-----------|------------|
| **ToGL** | `togl/` | OpenGL 2.1+ | Linux, macOS | `--use-togl` (default on non-Windows) |
| **ToGLES** | `togles/` | OpenGL ES 3.0 | Android, Linux | `--togles` |
| **ToGLESVk** | `toglesvk/` | Vulkan 1.1+ | Android 7.0+, Linux | `--use-vulkan` |

All three backends implement the same Direct3D 9 device interface (`IDirect3DDevice9`),
so the engine and material system code above them requires no changes.

### Vulkan Backend (toglesvk)

The `toglesvk` module is a DX9-to-Vulkan translation layer, structurally identical
to `togles` but targeting the Vulkan API instead of OpenGL ES.

**Architecture:**

```
MaterialSystem / shaderapidx9 (D3D9 API — unchanged)
        │
        ▼
rendermechanism.h  ← compile-time switch (DX_TO_VK_ABSTRACTION)
        │
        ▼
toglesvk module
  ├─ dxabstract.cpp     D3D9 device implementation (3300+ lines)
  ├─ vkcontext.cpp       Vulkan context: swapchain, pipelines, command buffers
  ├─ vkbuffer.cpp        Buffer management (VB/IB/UBO)
  ├─ vktex.cpp           Texture management (VkImage/VkImageView)
  ├─ vkfbo.cpp           Framebuffer management
  ├─ vkprogram.cpp       Shader modules (SPIR-V)
  ├─ vkquery.cpp         Occlusion/timestamp queries
  ├─ vkentrypoints.cpp   Vulkan function pointer loading (dlopen)
  └─ dx9asmvtospv.cpp    DX9 shader bytecode → SPIR-V translator
        │
        ▼
Vulkan API (libvulkan.so)
```

**Key design decisions:**

- D3D9 render states are mapped to Vulkan dynamic state and pipeline state objects
- DX9 shader bytecode (SM2.0/3.0) is translated to SPIR-V at runtime
- Vulkan function pointers are loaded via `dlopen("libvulkan.so")` at runtime,
  so the binary can run on API 21 while requiring API 24+ for actual Vulkan support
- Both `DX_TO_VK_ABSTRACTION` and `DX_TO_GL_ABSTRACTION` are defined simultaneously
  to maximize code reuse in `shaderapidx9`

For a detailed comparison of the three backends, see [docs/rendering-backends.md](docs/rendering-backends.md).

## Current Tasks

- ~~Rewrite materialsystem for OpenGL render~~ — superseded by Vulkan backend work
- Vulkan backend (`toglesvk`): improve DX9 → SPIR-V shader translator coverage
- Pipeline state caching for Vulkan (reduce per-frame pipeline creation overhead)
- Descriptor set pooling
- DXT → ETC2/ASTC texture transcoding for mobile
- dxvk-native support
- Elbrus port
- Bink audio support (for `video_bink`)

## How to Build

### Prerequisites

1. Initialize submodules (provides `thirdparty/`, `lib/`, `ivp/`):
   ```bash
   git submodule init && git submodule update
   ```

2. Install build dependencies (Linux example):
   ```
   SDL2, FreeType2, FontConfig, OpenAL, libjpeg, libpng, libcurl, zlib, bzip2
   ```

### Linux (x86 / x86_64)

```bash
# OpenGL backend (default)
./waf configure -T debug
./waf build

# Vulkan backend
./waf configure -T debug --use-vulkan
./waf build
```

### Android

Android builds produce native `.so` libraries. You need a separate Java/Gradle
wrapper project to package them into an APK.

```bash
# --- armv7-a (32-bit ARM) ---
# GLES backend (requires Android 5.0+ / API 21)
scripts/build-android-armv7a.sh

# Vulkan backend (requires Android 7.0+ / API 24)
scripts/build-android-vulkan.sh

# --- arm64-v8a (64-bit ARM / AArch64) ---
# GLES backend (requires Android 5.0+ / API 21, NDK r19+)
scripts/build-android-arm64.sh

# Vulkan backend (requires Android 7.0+ / API 24, NDK r19+)
scripts/build-android-arm64-vulkan.sh
```

Or configure manually:

```bash
# --- armv7-a (32-bit, NDK r10e, GCC 4.9) ---
export ANDROID_NDK_HOME=/path/to/android-ndk-r10e

# GLES
./waf configure -T debug --android=armeabi-v7a-hard,4.9,21 --togles --disable-warns
./waf build

# Vulkan
./waf configure -T debug --android=armeabi-v7a-hard,4.9,24 --use-vulkan --disable-warns
./waf build

# --- arm64-v8a (64-bit, NDK r20, Clang) ---
export ANDROID_NDK_HOME=/path/to/android-ndk-r20

# GLES
./waf configure -T debug --android=aarch64,clang,21 --togles --disable-warns
./waf build

# Vulkan
./waf configure -T debug --android=aarch64,clang,24 --use-vulkan --disable-warns
./waf build
```

Output locations:
- armv7-a: `build/android/armeabi-v7a/lib/`
- arm64-v8a: `build/android/arm64-v8a/lib/`

Copy `.so` files to your Android project's `jniLibs/<arch>/` directory.

For detailed Android build instructions, see [docs/build-android.md](docs/build-android.md).

### Windows

```batch
waf.bat configure -T debug
waf.bat build
```

Windows uses native DirectX 9 by default (no translation layer needed).

### macOS

```bash
scripts/build-macos-amd64.sh
```

### Dedicated Server (headless, no rendering)

Add `-d` / `--dedicated` flag to any build configuration.

### Build Options

| Option | Description |
|--------|-------------|
| `-T debug` / `-T release` | Build type |
| `--32bits` | Build 32-bit binaries |
| `-d` / `--dedicated` | Build dedicated server (no client/renderer) |
| `--tests` | Build unit tests |
| `--use-togl` | Use ToGL (OpenGL) backend |
| `--togles` | Use ToGLES (OpenGL ES) backend |
| `--use-vulkan` | Use Vulkan backend |
| `--use-sdl` | Use SDL2 for window/input |
| `--disable-warns` | Disable compiler warnings |
| `--sanitize` | Enable AddressSanitizer/MemorySanitizer |
| `--android=<arch>,<toolchain>,<api>` | Cross-compile for Android |

## Documentation

- [Building instructions (EN)](https://github.com/nillerusr/source-engine/wiki/Source-Engine-(EN))
- [Building instructions (RU)](https://github.com/nillerusr/source-engine/wiki/Source-Engine-(RU))
- [Android Vulkan Build Guide](docs/build-android.md)
- [Rendering Backends Comparison](docs/rendering-backends.md)
- [Vulkan Architecture](docs/vulkan-architecture.md)

## Project Structure

```
├── engine/              Core engine (host loop, networking, client/server)
├── materialsystem/      Material system + shader API (shaderapidx9)
├── togl/                DX9 → OpenGL translation layer
├── togles/              DX9 → OpenGL ES translation layer
├── toglesvk/            DX9 → Vulkan translation layer
├── public/              Public headers (shared interfaces)
│   ├── togl/            ToGL public headers
│   ├── togles/          ToGLES public headers
│   └── toglesvk/        ToGLESVk public headers
├── tier0/               Low-level: memory, threading, profiling
├── tier1/ vstdlib/      Standard library extensions
├── bitmap/              Image loading/processing
├── gcsdk/               Game Coordinator SDK (Steam GC communication)
├── launcher/            Engine launcher (incl. Android JNI bridge)
├── scripts/             Build scripts for each platform
├── docs/                Project documentation
└── wscript              Main build configuration
```

## Support me

BTC: `bc1qnjq92jj9uqjtafcx2zvnwd48q89hgtd6w8a6na`

ETH: `0x5d0D561146Ed758D266E59B56e85Af0b03ABAF46`

XMR: `48iXvX61MU24m5VGc77rXQYKmoww3dZh6hn7mEwDaLVTfGhyBKq2teoPpeBq6xvqj4itsGh6EzNTzBty6ZDDevApCFNpsJ`
