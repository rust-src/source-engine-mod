//========= Copyright Valve Corporation, All rights reserved. ============//
//                       TOGL CODE LICENSE
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.
//
// vkbase.h - Base Vulkan types and configuration constants
// Replaces togles/linuxwin/glbase.h
//
#ifndef VKBASE_H
#define VKBASE_H

#ifdef DX_TO_VK_ABSTRACTION

#include <vulkan/vulkan.h>

#ifdef USE_SDL
	#include "SDL.h"
	#include "SDL_vulkan.h"
#endif

#ifdef DX_TO_VK_ABSTRACTION
	#ifndef WIN32
	#define Debugger DebuggerBreak
	#endif
	#undef CurrentTime
#endif

// Set TOGL_SUPPORT_NULL_DEVICE to 1 to support the NULL ref device
#define TOGL_SUPPORT_NULL_DEVICE 0

#if TOGL_SUPPORT_NULL_DEVICE
	#define TOGL_NULL_DEVICE_CHECK if( m_params.m_deviceType == D3DDEVTYPE_NULLREF ) return S_OK;
	#define TOGL_NULL_DEVICE_CHECK_RET_VOID if( m_params.m_deviceType == D3DDEVTYPE_NULLREF ) return;
#else
	#define TOGL_NULL_DEVICE_CHECK
	#define TOGL_NULL_DEVICE_CHECK_RET_VOID
#endif

// VK_ENABLE_INDEX_VERIFICATION enables index range verification on all dynamic IB/VB's
#define VK_ENABLE_INDEX_VERIFICATION 0

// VK_ENABLE_UNLOCK_BUFFER_OVERWRITE_DETECTION
#define VK_ENABLE_UNLOCK_BUFFER_OVERWRITE_DETECTION 0

#define VK_BATCH_TELEMETRY_ZONES 0

// VK_BATCH_PERF_ANALYSIS - Enables batch visualization and per-batch telemetry
#define VK_BATCH_PERF_ANALYSIS 0
#define VK_BATCH_PERF_ANALYSIS_WRITE_PNGS 0

// VK_TELEMETRY_ZONES - Causes every single Vulkan call to generate a telemetry event
#define VK_TELEMETRY_ZONES 0

// VK_DUMP_ALL_API_CALLS - Causes a debug message to be printed for every API call
#define VK_DUMP_ALL_API_CALLS 0

// VK_TELEMETRY_GPU_ZONES - GPU telemetry zones
#define VK_TELEMETRY_GPU_ZONES 0

// Records global # of Vulkan calls/total cycles spent
#define VK_TRACK_API_TIME VK_BATCH_PERF_ANALYSIS

#define VK_USE_EXECUTE_HELPER_FOR_ALL_API_CALLS ( VK_TELEMETRY_ZONES || VK_TRACK_API_TIME || VK_DUMP_ALL_API_CALLS )

#if VK_BATCH_PERF_ANALYSIS
	#define VK_BATCH_PERF(...) __VA_ARGS__
#else
	#define VK_BATCH_PERF(...)
#endif

// Number of user clip planes supported
#define kVKUserClipPlanes 2

// Number of scratch framebuffers
#define kVKScratchFBOCount 4

// Max concurrent frames in flight
#define MAX_FRAMES_IN_FLIGHT 2

// Max render targets (MRT)
#define MAX_RENDER_TARGETS 4

// Max texture samplers
#define VK_SAMPLER_COUNT 16

// Max vertex attributes
#define MAX_VERTEX_ATTRIBUTES 16

// Max vertex shader constants
#define MAX_VS_CONSTANTS 256

// Max pixel shader constants
#define MAX_PS_CONSTANTS 32

// Vulkan API version we target
#define VULKAN_API_VERSION VK_API_VERSION_1_1

// Debug printf macros
#define VKMPRINTF(args)
#define VKMPRINTSTR(args)
#define VKMPRINTTEXT(args)

// Driver provider enumeration
enum VKDriverProvider
{
	cVKDriverProviderUnknown = 0,
	cVKDriverProviderNVIDIA,
	cVKDriverProviderAMD,
	cVKDriverProviderIntelOpenSource,
	cVKDriverProviderQualcomm,
	cVKDriverProviderARM,
	cVKDriverProviderImagination,
	cVKDriverProviderApple,
};

// Vendor IDs
#define VK_VENDOR_ID_NVIDIA   0x10DE
#define VK_VENDOR_ID_AMD       0x1002
#define VK_VENDOR_ID_INTEL     0x8086
#define VK_VENDOR_ID_QUALCOMM  0x5143
#define VK_VENDOR_ID_ARM       0x13B5
#define VK_VENDOR_ID_IMGTEC    0x1010

#endif // DX_TO_VK_ABSTRACTION

#endif // VKBASE_H
