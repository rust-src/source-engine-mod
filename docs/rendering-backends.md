# Rendering Backends Comparison

This document compares the three rendering backends available in this fork
of the Source Engine.

## Summary

All three backends implement the same Direct3D 9 (`IDirect3DDevice9`) interface.
The engine and material system code is identical regardless of backend — only
the translation layer and the underlying graphics API differ.

| | ToGL | ToGLES | ToGLESVk |
|---|------|--------|----------|
| **Module** | `togl/` | `togles/` | `toglesvk/` |
| **Target API** | OpenGL 2.1+ | OpenGL ES 3.0 | Vulkan 1.1+ |
| **Platforms** | Linux, macOS | Android, Linux | Android 7.0+, Linux |
| **Build flag** | `--use-togl` | `--togles` | `--use-vulkan` |
| **Shader format** | GLSL (runtime) | GLSL ES 300 (runtime) | SPIR-V (runtime) |
| **Context class** | `GLMContext` | `GLMContext` | `CVKContext` |
| **Texture wrapper** | `CGLMTex` | `CGLMTex` | `CVKTex` |
| **Buffer wrapper** | `CGLMBuffer` | `CGLMBuffer` | `CVKBuffer` |
| **Shader wrapper** | `CGLMProgram` | `CGLMProgram` | `CVKProgram` |
| **Framebuffer** | `CGLMFBO` | `CGLMFBO` | `CVKFramebuffer` |
| **Function loading** | GLX/EGL + dlsym | EGL + dlsym | dlopen + vkGetProcAddr |
| **Maturity** | Production | Production | Experimental |

## Architecture Comparison

### Common Layer (shared by all backends)

```
┌────────────────────────────────────────────┐
│  Engine (host.cpp, cl_main.cpp, etc.)      │
├────────────────────────────────────────────┤
│  MaterialSystem (cmaterialsystem.cpp)       │
├────────────────────────────────────────────┤
│  shaderapidx9 (shaderapidx8.cpp)            │
│  Calls IDirect3DDevice9 methods             │
├────────────────────────────────────────────┤
│  rendermechanism.h ← compile-time switch    │
└────────────────────────────────────────────┘
```

Everything above `rendermechanism.h` is identical across all backends.

### ToGL (OpenGL)

```
rendermechanism.h
    → togles/rendermechanism.h (if DX_TO_GL_ABSTRACTION)
        → dxabstract.h / dxabstract.cpp
            → GLMContext (glmgr.cpp)
                → OpenGL API (libGL.so)
```

- Uses GLX (Linux) or CGL (macOS) for context creation
- DX9 ASM → GLSL translation at runtime (`dx9asmtogl2.cpp`)
- FBOs for render targets
- Most mature non-Windows backend

### ToGLES (OpenGL ES)

```
rendermechanism.h
    → togles/rendermechanism.h (if DX_TO_GL_ABSTRACTION)
        → dxabstract.h / dxabstract.cpp
            → GLMContext (glmgr.cpp)
                → OpenGL ES API (libGLESv2.so)
```

- Uses EGL for context creation (via SDL2)
- DX9 ASM → GLSL ES 300 translation at runtime (`dx9asmtogl2.cpp`)
- Identical class structure to ToGL (near-duplicate codebase)
- Primary Android backend

### ToGLESVk (Vulkan)

```
rendermechanism.h
    → toglesvk/rendermechanism.h (if DX_TO_VK_ABSTRACTION)
        → dxabstract.h / dxabstract.cpp
            → CVKContext (vkcontext.cpp)
                → Vulkan API (libvulkan.so)
```

- Uses `dlopen("libvulkan.so")` for function loading
- DX9 ASM → SPIR-V translation at runtime (`dx9asmvtospv.cpp`)
- `VkRenderPass` + `VkFramebuffer` for render targets
- `VkPipeline` objects for graphics state (cached via `VkPipelineCache`)
- Explicit command buffer recording (vs. implicit GL state machine)
- New codebase, not a copy of togles (though interface-compatible)

## State Management

### ToGL / ToGLES (Implicit State Machine)

OpenGL uses an implicit global state machine. The `GLMContext` caches state
and only calls `gl*` functions when state actually changes:

```cpp
void GLMContext::WriteDepthTestEnable(bool enable) {
    if (m_state.depthTestEnable != enable) {
        m_state.depthTestEnable = enable;
        glEnable(GL_DEPTH_TEST);  // or glDisable
    }
}
```

**Pros**: Simple, low overhead for unchanged state
**Cons**: State validation happens in the driver on every draw; driver may
recompile shaders internally; no explicit control over pipeline state

### ToGLESVk (Explicit Pipeline State)

Vulkan requires explicit pipeline state objects. State changes may require
creating a new `VkPipeline`:

```cpp
void CVKContext::FlushDrawStates() {
    // Hash current state → look up pipeline in cache
    // If miss: create new VkGraphicsPipelineCreateInfo → vkCreateGraphicsPipelines
    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    // Set dynamic state
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    // Bind descriptors, vertex buffers, etc.
}
```

**Pros**: No driver validation overhead; deterministic performance; multi-threaded
command buffer recording possible
**Cons**: Pipeline creation is expensive (needs caching); more complex implementation;
state changes are not "free" like in GL

## Shader Translation

### ToGL / ToGLES

```
HLSL → (fxc, offline) → DX9 SM3.0 ASM → (dx9asmtogl2.cpp, runtime) → GLSL/GLSL ES → (driver) → GPU ISA
```

The DX9 ASM → GLSL translator generates human-readable GLSL with variable names
like `r0`, `r1`, `c0`... The GPU driver's GLSL compiler then optimizes this.
Driver GLSL compiler quality varies significantly, especially on mobile GPUs.

### ToGLESVk

```
HLSL → (fxc, offline) → DX9 SM3.0 ASM → (dx9asmvtospv.cpp, runtime) → SPIR-V → (driver) → GPU ISA
```

SPIR-V is a binary intermediate format. The driver's SPIR-V optimizer is generally
more consistent across vendors than GLSL compilers. However, the generated SPIR-V
from DX9 ASM is still not optimal (machine-generated, unoptimized).

## Performance Characteristics

### Where ToGLESVk Wins

1. **Draw call overhead**: Vulkan has lower per-draw-call overhead than GLES,
   especially on Adreno GPUs. For draw-call-heavy scenes (BSP rendering with
   many batches), Vulkan can be 20-40% faster.

2. **Multi-threading**: Vulkan supports multi-threaded command buffer recording,
   which GLES cannot do. (Not yet implemented in toglesvk, but architecturally
   possible.)

3. **Pipeline state validation**: Vulkan pre-validates pipeline state at creation
   time. GLES validates at every draw, which is a hidden cost.

4. **Memory control**: Vulkan's explicit memory management allows better control
   over memory placement (DEVICE_LOCAL vs. HOST_VISIBLE), potentially reducing
   bandwidth.

### Where ToGLES Wins

1. **Maturity**: ToGLES is production-tested. ToGLESVk is experimental with
   incomplete shader translation.

2. **Simplicity**: The GL state machine is simpler to reason about. Vulkan's
   explicit pipeline objects add complexity.

3. **Driver support**: GLES 3.0 is available on virtually all Android 5.0+
   devices. Vulkan requires Android 7.0+ with a capable GPU driver.

4. **State changes**: In GL, simple state changes (e.g., toggling depth test)
   are a single function call. In Vulkan, they may require creating a new
   pipeline object (though pipeline caching mitigates this).

## When to Use Which Backend

| Use Case | Recommended Backend | Reason |
|----------|-------------------|--------|
| Production Android app | ToGLES | Proven, stable, widest device support |
| Android development/testing | ToGLESVk | Test Vulkan path, find bugs |
| Linux desktop | ToGL | Mature, well-tested on desktop |
| Linux experimental | ToGLESVk | Test Vulkan on desktop |
| Low-end Android (API < 24) | ToGLES | Vulkan not available |
| High-end Android (Vulkan-capable) | ToGLESVk | Potential performance gains |
| Windows | Native D3D9 | No translation needed |

## Code Reuse Between Backends

ToGL and ToGLES share ~95% of their code (near-duplicate files with minor
differences in GL version and extensions). ToGLESVk is a fresh implementation
but follows the same interface contract.

The D3D9 type definitions (`dxabstract_types.h`) are identical across all three
backends — only the backend class names change:

| ToGL/ToGLES | ToGLESVk |
|-------------|----------|
| `GLMContext` | `CVKContext` |
| `CGLMTex` | `CVKTex` |
| `CGLMBuffer` | `CVKBuffer` |
| `CGLMProgram` | `CVKProgram` |
| `CGLMFBO` | `CVKFramebuffer` |
| `CGLMQuery` | `CVKQuery` |
| `GLMRect` | `VKRect` |
| `GLMShaderPairInfo` | `VKShaderPairInfo` |
| `GLMPRINTF` | `VKMPRINTF` |

## Future Unified Backend

A potential future improvement would be to merge the common code between
ToGL, ToGLES, and ToGLESVk into a shared library, with only the API-specific
calls abstracted behind a thin interface. This would reduce maintenance burden
and ensure bug fixes apply to all backends simultaneously.
