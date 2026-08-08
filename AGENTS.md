# Source Engine (Android/Linux/Windows/macOS)

Port of Valve's Source Engine (TF2 2018 leak) to Android, Linux, macOS, Windows. Uses waf build system.

## Build Commands

### Linux x86_64
```bash
python3 ./waf configure -T release --disable-warns --prefix=./linux --build-games=hl2
python3 ./waf build -j$(nproc)
python3 ./waf install
```

### Android aarch64
```bash
scripts/build-android-arm64.sh
```
Downloads NDK r10e + Clang 11.1.0, builds FFmpeg, then waf with `--togles`.

### Android armv7a
```bash
scripts/build-android-armv7a.sh
```

### Windows
```bash
./waf.bat configure -T release --prefix=./win --disable-warns
./waf.bat install
```

### macOS
```bash
scripts/build-macos-amd64.sh --prefix=./macos
./waf install
```

## Structure

- `wscript` — master waf build config, project list, platform defines
- `scripts/build-android-*.sh` — Android build scripts (NDK r10e, Clang 11.1.0)
- `scripts/build-ffmpeg-android.sh` — FFmpeg static build for Android (called by android scripts)
- `scripts/waifulib/xcompile.py` — cross-compilation for Android (NDK detection, sysroot, toolchain)
- `appframework/sdlmgr.cpp` — SDL manager, includes `glmdisplaydb_linuxwin.inl`
- `appframework/glmdisplaydb_linuxwin.inl` — renderer info init (GPU detection lives here)
- `togl/` — OpenGL renderer (Linux/Windows/macOS)
- `togles/` — OpenGL ES renderer (Android)
- `public/togl/linuxwin/glmdisplay.h` — `GLMRendererInfoFields` struct
- `public/togles/linuxwin/glmdisplay.h` — same struct, GLES variant
- `materialsystem/` — material system + shaders
- `game/client/` and `game/server/` — game logic

## Key Architecture

- **togl** vs **togles**: `togl/` = desktop OpenGL, `togles/` = mobile OpenGL ES. Selected via `--togles` flag. Headers are in `public/togl/` and `public/togles/`.
- **glmdisplaydb_linuxwin.inl**: Shared `.inl` file included by `sdlmgr.cpp`. Contains `GLMRendererInfo::Init()` with GPU capability detection.
- **GLMRendererInfoFields** struct in `glmdisplay.h`: Defines GPU caps, driver quirks, and workaround flags. Both `togl` and `togles` have their own copy.
- **waf configure**: `--android=ARCH,TOOLCHAIN,API` for Android. `TOOLCHAIN=host` uses NDK's clang directly.
- **NDK r10e**: Android builds use this old NDK (android-21 minimum). Sysroot at `platforms/android-21/arch-ARCH`.

## CI

- `.github/workflows/build.yml` — Linux, Android, Windows, macOS builds
- `.github/workflows/android_2023.yml` — APK builds (clones modlauncher-waf)
- Android CI runs `build-android-armv7a.sh` and `build-android-arm64.sh` sequentially

## Gotchas

- `togl` and `togles` have separate `glmdisplay.h` — changes to `GLMRendererInfoFields` must be applied to BOTH `public/togl/linuxwin/glmdisplay.h` AND `public/togles/linuxwin/glmdisplay.h`.
- `glmdisplaydb_linuxwin.inl` is in `appframework/` (shared), NOT in `togl/` or `togles/`.
- Android build scripts hardcode NDK r10e paths. `scripts/waifulib/xcompile.py` has `ANDROID_NDK_SUPPORTED = [10, 19, 20]`.
- `_GLIBCXX_USE_CXX11_ABI=0` is defined for Linux builds.
- `--disable-warns` flag suppresses warnings in configure (adds `-w` to compiler flags).
