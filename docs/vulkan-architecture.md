# Vulkan Backend Architecture (toglesvk)

This document describes the architecture of the `toglesvk` module — the DX9-to-Vulkan
translation layer that serves as an alternative to `togles` (DX9-to-OpenGL ES).

## Overview

The Source Engine's renderer is built on Direct3D 9. On non-Windows platforms, the
engine uses a translation layer to intercept D3D9 calls and redirect them to a
native graphics API. `toglesvk` translates these calls to Vulkan 1.1+.

The core principle: **the D3D9 API surface is the stable ABI**. The engine, material
system, and shader system (`shaderapidx9`) call `IDirect3DDevice9` methods without
knowing whether the backend is real D3D9 (Windows), OpenGL (togl), OpenGL ES (togles),
or Vulkan (toglesvk).

## Compile-Time Switch

The backend is selected at compile time via preprocessor macros in
`public/togles/rendermechanism.h`:

```cpp
#if defined(DX_TO_VK_ABSTRACTION)
    // Vulkan backend: includes toglesvk headers
    #include "toglesvk/linuxwin/dxabstract.h"
    // ...
#elif defined(DX_TO_GL_ABSTRACTION)
    // OpenGL/GLES backend: includes togles headers
    #include "togles/linuxwin/dxabstract.h"
    // ...
#else
    // Windows: real D3D9 SDK headers
    #include <d3d9.h>
#endif
```

When `--use-vulkan` is passed to waf, both `DX_TO_VK_ABSTRACTION` and
`DX_TO_GL_ABSTRACTION` are defined. The Vulkan branch is checked first in
`rendermechanism.h`, so Vulkan headers take precedence. The `DX_TO_GL_ABSTRACTION`
define allows `shaderapidx9` code (which has `#ifdef DX_TO_GL_ABSTRACTION` guards)
to compile without modification.

## Module Structure

### Public Headers (`public/toglesvk/linuxwin/`)

| File | Purpose |
|------|---------|
| `rendermechanism.h` | Top-level include that switches to Vulkan backend |
| `vkbase.h` | Base types, configuration constants, GL compat defines |
| `vkentrypoints.h` | `CVulkanEntryPoints` class — loads Vulkan function pointers |
| `vkcontext.h` | `CVKContext` — main Vulkan context (replaces `GLMContext`) |
| `vkbuffer.h` | `CVKBuffer` — vertex/index/uniform buffer wrapper |
| `vktex.h` | `CVKTex` — texture wrapper (VkImage/VkImageView) |
| `vkfbo.h` | `CVKFramebuffer` — framebuffer wrapper (replaces `CGLMFBO`) |
| `vkprogram.h` | `CVKProgram` — SPIR-V shader module wrapper |
| `vkquery.h` | `CVKQuery` — occlusion/timestamp query wrapper |
| `dxabstract_types.h` | D3D9 type definitions (identical to togles version) |
| `dxabstract.h` | D3D9 interface declarations (IDirect3DDevice9, etc.) |

### Implementation (`toglesvk/linuxwin/`)

| File | Lines | Purpose |
|------|-------|---------|
| `dxabstract.cpp` | ~3300 | D3D9 device implementation — all `IDirect3DDevice9` methods |
| `vkcontext.cpp` | ~1800 | Vulkan context: instance, device, swapchain, command buffers, pipeline state |
| `dx9asmvtospv.cpp` | ~1700 | DX9 shader bytecode → SPIR-V translator |
| `vkentrypoints.cpp` | ~400 | `dlopen("libvulkan.so")` + function pointer loading |
| `vkbuffer.cpp` | ~250 | Buffer creation, locking, memory mapping |
| `vktex.cpp` | ~400 | Texture creation, locking, layout transitions |
| `vkfbo.cpp` | ~100 | Framebuffer creation from render targets |
| `vkprogram.cpp` | ~100 | Shader module creation from SPIR-V |
| `vkquery.cpp` | ~100 | Query pool management |

## Key Classes

### CVulkanEntryPoints

Loads all Vulkan function pointers at runtime via `dlopen` + `vkGetInstanceProcAddr`.
On Android, loads `libvulkan.so`. On Linux, loads `libvulkan.so.1`.

Selects a physical device based on:
- Discrete GPU preferred over integrated
- Graphics queue family support
- Swapchain extension support

### CVKContext

The central Vulkan state machine, replacing `GLMContext` from togles. Manages:

- **Swapchain**: `VkSwapchainKHR` with 2 images (double-buffered)
- **Command buffers**: One per frame (MAX_FRAMES_IN_FLIGHT = 2), plus separate
  upload command buffers for texture/buffer data
- **Render pass**: Single `VkRenderPass` with up to 4 color attachments + depth
- **Pipeline cache**: `VkPipelineCache` for faster pipeline creation
- **Descriptor sets**: Pool + layout for sampler bindings
- **State tracking**: Cached D3D9 render state buckets (depth, blend, stencil, etc.)
  that are flushed to Vulkan dynamic state at draw time
- **Framebuffer cache**: Maps render target combinations to `VkFramebuffer` objects

### DX9 → Vulkan State Mapping

| D3D9 Render State | Vulkan Equivalent |
|-------------------|-------------------|
| `D3DRS_ZENABLE` | `VkPipelineDepthStencilStateCreateInfo::depthTestEnable` |
| `D3DRS_ZWRITEENABLE` | `depthWriteEnable` |
| `D3DRS_ZFUNC` | `depthCompareOp` (dynamic state) |
| `D3DRS_CULLMODE` | `cullMode` in `VkPipelineRasterizationStateCreateInfo` |
| `D3DRS_ALPHABLENDENABLE` | `blendEnable` per attachment |
| `D3DRS_SRCBLEND` / `D3DRS_DESTBLEND` | `srcColorBlendFactor` / `dstColorBlendFactor` |
| `D3DRS_BLENDOP` | `colorBlendOp` |
| `D3DRS_STENCIL_ENABLE` | `stencilTestEnable` |
| `D3DRS_STENCILFUNC` | `front.compareOp` (dynamic state) |
| `D3DRS_STENCILFAIL` / `STENCILPASS` | `front.failOp` / `front.passOp` |
| `D3DRS_SCISSORTESTENABLE` | Dynamic scissor via `vkCmdSetScissor` |
| `D3DRS_DEPTHBIAS` | Dynamic via `vkCmdSetDepthBias` |

### Shader Translation Pipeline

```
HLSL source (.fxc/.vxc)
    │  (offline, at build time)
    ▼
DX9 Shader Model 2.0/3.0 bytecode
    │  (at runtime, in CreatePixelShader/CreateVertexShader)
    ▼
CD3DToVK::Translate()
    │  parses DX9 tokens, generates SPIR-V binary
    ▼
VkShaderModule (via vkCreateShaderModule)
    │  (at draw time, linked into VkGraphicsPipeline)
    ▼
GPU execution
```

The translator (`dx9asmvtospv.cpp`) handles:
- DX9 token parsing (version, instructions, register declarations)
- SPIR-V header, capability declarations, type generation
- Common ALU opcodes: `mov`, `add`, `mul`, `mad`, `dp3`, `dp4`, `rsq`, `rcp`, etc.
- Texture sampling: `texld`, `texcrd`
- Register types: input (v0/v1...), output (oPos, oD0...), constant (c0...), temp (r0...)

Unsupported opcodes emit a warning and pass through as no-ops.

## Vulkan Function Loading

On Android and Linux, Vulkan functions are loaded at runtime rather than linked
statically. This allows:

1. The binary to load on devices without Vulkan (graceful failure)
2. Selective extension loading based on device capabilities
3. No hard dependency on a Vulkan SDK at link time

```cpp
// Simplified flow
void *lib = dlopen("libvulkan.so", RTLD_NOW);
auto vkGetInstanceProcAddr = dlsym(lib, "vkGetInstanceProcAddr");
// Create instance, then load device-level functions
```

## Frame Execution Model

```
Frame N:
  1. vkAcquireNextImageKHR() → get swapchain image index
  2. vkBeginCommandBuffer()
  3. [Engine renders frame: multiple draw calls]
     - Each draw: flush state → bind pipeline → bind descriptors → vkCmdDraw*
  4. vkEndCommandBuffer()
  5. vkQueueSubmit() with render fence + semaphores
  6. vkQueuePresentKHR()

Frame N+1:
  - Wait for Frame N-1's fence (double-buffered, so Frame N is still in flight)
  - Repeat
```

## Memory Management

- **DEVICE_LOCAL**: Used for textures and static buffers (fast GPU access)
- **HOST_VISIBLE | HOST_COHERENT**: Used for dynamic vertex/index buffers
  with persistent mapping for efficient CPU updates
- **Staging buffers**: Used to upload data to DEVICE_LOCAL memory
  (create staging → memcpy → vkCmdCopyBuffer → destroy staging)

Memory type selection uses `vkGetPhysicalDeviceMemoryProperties` to find the
best match for the requested properties.

## Limitations and Future Work

1. **Shader translator coverage**: Not all DX9 opcodes are implemented yet.
   Complex shaders may render incorrectly.
2. **Pipeline cache**: Graphics pipelines are created per-draw currently.
   A pipeline cache keyed on state hash is needed for performance.
3. **Descriptor set pooling**: Descriptor sets should be pooled rather than
   allocated/freed per frame.
4. **Texture compression**: DXT (S3TC) textures need runtime transcoding to
   ETC2/ASTC on GPUs without S3TC support (most Adreno/Mali).
5. **Multi-threading**: Command buffer recording is single-threaded. Vulkan
   supports multi-threaded recording which could improve performance.
