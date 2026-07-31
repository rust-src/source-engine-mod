//========= Copyright Valve Corporation, All rights reserved. ============//
//                       TOGL CODE LICENSE
//
//  Copyright 2011-2014 Valve Corporation
//  All Rights Reserved.
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
// vkcontext.cpp
//
// The main Vulkan context - replaces GLMContext from the GL backend. Owns the
// swapchain, per-frame command buffers / sync objects, render pass, pipeline
// cache, descriptor layouts and the framebuffer cache. Maps D3D9 render state
// onto Vulkan dynamic state and records draw commands.
//
//===============================================================================
#include "toglesvk/rendermechanism.h"

#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include "tier0/vprof.h"
#include "tier0/platform.h"

#if !defined( _WIN32 )
	#include <dlfcn.h>
	#if !defined( USE_SDL ) && !defined( __ANDROID__ )
		#include <X11/Xlib.h>
	#endif
	#ifdef __ANDROID__
		#include <android/native_window.h>
	#endif
#endif

#ifdef USE_SDL
	#include "SDL.h"
	#include "SDL_vulkan.h"
#endif

// memdbgon -must- be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

#if !defined( DX_TO_VK_ABSTRACTION )
#error vkcontext.cpp must only be compiled under DX_TO_VK_ABSTRACTION
#endif

//=============================================================================
// D3D9 -> Vulkan conversion tables. Mirror the D3D*ToGL* helpers in glmgr.cpp.
//=============================================================================

VkCompareOp D3DCompareFuncToVK( DWORD func )
{
	switch ( func )
	{
		case D3DCMP_NEVER:        return VK_COMPARE_OP_NEVER;
		case D3DCMP_LESS:         return VK_COMPARE_OP_LESS;
		case D3DCMP_EQUAL:        return VK_COMPARE_OP_EQUAL;
		case D3DCMP_LESSEQUAL:    return VK_COMPARE_OP_LESS_OR_EQUAL;
		case D3DCMP_GREATER:      return VK_COMPARE_OP_GREATER;
		case D3DCMP_NOTEQUAL:     return VK_COMPARE_OP_NOT_EQUAL;
		case D3DCMP_GREATEREQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case D3DCMP_ALWAYS:       return VK_COMPARE_OP_ALWAYS;
		default:                  Assert( !"D3DCompareFuncToVK: unknown func" ); return VK_COMPARE_OP_ALWAYS;
	}
}

VkBlendOp D3DBlendOperationToVK( DWORD op )
{
	switch ( op )
	{
		case D3DBLENDOP_ADD:         return VK_BLEND_OP_ADD;
		case D3DBLENDOP_SUBTRACT:    return VK_BLEND_OP_SUBTRACT;
		case D3DBLENDOP_REVSUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
		case D3DBLENDOP_MIN:         return VK_BLEND_OP_MIN;
		case D3DBLENDOP_MAX:         return VK_BLEND_OP_MAX;
		default:                     Assert( !"D3DBlendOperationToVK: unknown op" ); return VK_BLEND_OP_ADD;
	}
}

VkBlendFactor D3DBlendFactorToVK( DWORD factor )
{
	switch ( factor )
	{
		case D3DBLEND_ZERO:            return VK_BLEND_FACTOR_ZERO;
		case D3DBLEND_ONE:             return VK_BLEND_FACTOR_ONE;
		case D3DBLEND_SRCCOLOR:        return VK_BLEND_FACTOR_SRC_COLOR;
		case D3DBLEND_INVSRCCOLOR:     return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case D3DBLEND_SRCALPHA:        return VK_BLEND_FACTOR_SRC_ALPHA;
		case D3DBLEND_INVSRCALPHA:     return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case D3DBLEND_DESTALPHA:       return VK_BLEND_FACTOR_DST_ALPHA;
		case D3DBLEND_INVDESTALPHA:    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case D3DBLEND_DESTCOLOR:       return VK_BLEND_FACTOR_DST_COLOR;
		case D3DBLEND_INVDESTCOLOR:    return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case D3DBLEND_SRCALPHASAT:     return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
		case D3DBLEND_BOTHSRCALPHA:    return VK_BLEND_FACTOR_SRC_ALPHA;
		case D3DBLEND_BOTHINVSRCALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case D3DBLEND_BLENDFACTOR:     return VK_BLEND_FACTOR_CONSTANT_COLOR;
		default:                       Assert( !"D3DBlendFactorToVK: unknown factor" ); return VK_BLEND_FACTOR_ONE;
	}
}

VkStencilOp D3DStencilOpToVK( DWORD op )
{
	switch ( op )
	{
		case D3DSTENCILOP_KEEP:     return VK_STENCIL_OP_KEEP;
		case D3DSTENCILOP_ZERO:     return VK_STENCIL_OP_ZERO;
		case D3DSTENCILOP_REPLACE:  return VK_STENCIL_OP_REPLACE;
		case D3DSTENCILOP_INCRSAT:  return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
		case D3DSTENCILOP_DECRSAT:  return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
		case D3DSTENCILOP_INVERT:   return VK_STENCIL_OP_INVERT;
		case D3DSTENCILOP_INCR:     return VK_STENCIL_OP_INCREMENT_AND_WRAP;
		case D3DSTENCILOP_DECR:     return VK_STENCIL_OP_DECREMENT_AND_WRAP;
		default:                    Assert( !"D3DStencilOpToVK: unknown op" ); return VK_STENCIL_OP_KEEP;
	}
}

VkPrimitiveTopology D3DPrimitiveTypeToVK( DWORD type )
{
	switch ( type )
	{
		case D3DPT_POINTLIST:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case D3DPT_LINELIST:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case D3DPT_TRIANGLELIST:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case D3DPT_TRIANGLESTRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		default:                  Assert( !"D3DPrimitiveTypeToVK: unknown type" ); return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}
}

VkCullModeFlags D3DCullModeToVK( DWORD mode )
{
	switch ( mode )
	{
		case D3DCULL_NONE: return VK_CULL_MODE_NONE;
		case D3DCULL_CW:   return VK_CULL_MODE_FRONT_BIT;
		case D3DCULL_CCW:  return VK_CULL_MODE_BACK_BIT;
		default:           Assert( !"D3DCullModeToVK: unknown mode" ); return VK_CULL_MODE_NONE;
	}
}

VkFormat D3DFormatToVKFormat( DWORD format )
{
	switch ( format )
	{
		case D3DFMT_UNKNOWN:        return VK_FORMAT_UNDEFINED;
		case D3DFMT_A8R8G8B8:       return VK_FORMAT_B8G8R8A8_UNORM;
		case D3DFMT_X8R8G8B8:       return VK_FORMAT_B8G8R8A8_UNORM;
		case D3DFMT_R5G6B5:         return VK_FORMAT_R5G6B5_UNORM_PACK16;
		case D3DFMT_A1R5G5B5:       return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
		case D3DFMT_A4R4G4B4:       return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
		case D3DFMT_A8:             return VK_FORMAT_R8_UNORM;
		case D3DFMT_A8B8G8R8:       return VK_FORMAT_R8G8B8A8_UNORM;
		case D3DFMT_X8B8G8R8:       return VK_FORMAT_R8G8B8A8_UNORM;
		case D3DFMT_G16R16:         return VK_FORMAT_R16G16_UNORM;
		case D3DFMT_A2B10G10R10:    return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		case D3DFMT_A16B16G16R16:   return VK_FORMAT_R16G16B16A16_UNORM;
		case D3DFMT_L8:             return VK_FORMAT_R8_UNORM;
		case D3DFMT_A8L8:           return VK_FORMAT_R8G8_UNORM;
		case D3DFMT_L16:            return VK_FORMAT_R16_UNORM;
		case D3DFMT_V8U8:           return VK_FORMAT_R8G8_SNORM;
		case D3DFMT_Q8W8V8U8:       return VK_FORMAT_R8G8B8A8_SNORM;
		case D3DFMT_V16U16:         return VK_FORMAT_R16G16_SNORM;
		case D3DFMT_A2R10G10B10:    return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
		case D3DFMT_R8G8B8:         return VK_FORMAT_R8G8B8_UNORM;
		case D3DFMT_D16:            return VK_FORMAT_D16_UNORM;
		case D3DFMT_D24S8:          return VK_FORMAT_D24_UNORM_S8_UINT;
		case D3DFMT_D24X8:          return VK_FORMAT_X8_D24_UNORM_PACK32;
		case D3DFMT_D16_LOCKABLE:   return VK_FORMAT_D16_UNORM;
		case D3DFMT_D32:            return VK_FORMAT_D32_SFLOAT;
		case D3DFMT_INDEX16:        return VK_FORMAT_R16_UINT;
		case D3DFMT_INDEX32:        return VK_FORMAT_R32_UINT;
		case D3DFMT_R16F:           return VK_FORMAT_R16_SFLOAT;
		case D3DFMT_G16R16F:        return VK_FORMAT_R16G16_SFLOAT;
		case D3DFMT_A16B16G16R16F:  return VK_FORMAT_R16G16B16A16_SFLOAT;
		case D3DFMT_R32F:           return VK_FORMAT_R32_SFLOAT;
		case D3DFMT_A32B32G32R32F:  return VK_FORMAT_R32G32B32A32_SFLOAT;
		case D3DFMT_DXT1:           return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		case D3DFMT_DXT3:           return VK_FORMAT_BC2_UNORM_BLOCK;
		case D3DFMT_DXT5:           return VK_FORMAT_BC3_UNORM_BLOCK;
		default:                    Assert( !"D3DFormatToVKFormat: unknown format" ); return VK_FORMAT_UNDEFINED;
	}
}

VkPolygonMode D3DFillModeToVK( DWORD mode )
{
	switch ( mode )
	{
		case D3DFILL_POINT:      return VK_POLYGON_MODE_POINT;
		case D3DFILL_WIREFRAME:  return VK_POLYGON_MODE_LINE;
		case D3DFILL_SOLID:      return VK_POLYGON_MODE_FILL;
		default:                 Assert( !"D3DFillModeToVK: unknown mode" ); return VK_POLYGON_MODE_FILL;
	}
}

VkFilter D3DTextureFilterToVK( DWORD filter )
{
	switch ( filter )
	{
		case D3DTEXF_NONE:
		case D3DTEXF_POINT:        return VK_FILTER_NEAREST;
		case D3DTEXF_LINEAR:
		case D3DTEXF_ANISOTROPIC:  return VK_FILTER_LINEAR;
		default:                   Assert( !"D3DTextureFilterToVK: unknown filter" ); return VK_FILTER_LINEAR;
	}
}

VkSamplerAddressMode D3DTextureAddressToVK( DWORD address )
{
	switch ( address )
	{
		case D3DTADDRESS_WRAP:    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case D3DTADDRESS_CLAMP:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case D3DTADDRESS_BORDER:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		default:                   Assert( !"D3DTextureAddressToVK: unknown address" ); return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

//=============================================================================
// CVKFramebufferMap - simple linear cache keyed on the bound render target set.
//=============================================================================

CVKFramebuffer *CVKFramebufferMap::FindOrCreate( const RenderTargetState_t &key, CVKContext *ctx )
{
	for ( int i = 0; i < m_entries.Count(); ++i )
	{
		if ( m_entries[i].key == key )
		{
			return m_entries[i].value;
		}
	}

	CVKFramebuffer *fbo = new CVKFramebuffer();
	VkRenderPass renderPass = ctx->GetRenderPass();

	int rtCount = 0;
	for ( int i = 0; i < MAX_RENDER_TARGETS; ++i )
	{
		if ( key.m_pRenderTargets[i] )
		{
			rtCount = i + 1;
		}
	}

	if ( !fbo->Create( ctx, (CVKTex **)key.m_pRenderTargets, rtCount, key.m_pDepthStencil, renderPass ) )
	{
		Warning( "CVKFramebufferMap::FindOrCreate: CVKFramebuffer::Create failed\n" );
		delete fbo;
		return NULL;
	}

	Entry e;
	e.key = key;
	e.value = fbo;
	m_entries.AddToTail( e );
	return fbo;
}

void CVKFramebufferMap::DestroyAll()
{
	for ( int i = 0; i < m_entries.Count(); ++i )
	{
		if ( m_entries[i].value )
		{
			m_entries[i].value->Destroy();
			delete m_entries[i].value;
			m_entries[i].value = NULL;
		}
	}
	m_entries.Purge();
}

//=============================================================================
// CVKContext
//=============================================================================

CVKContext::CVKContext()
{
	m_surface = VK_NULL_HANDLE;
	m_swapchain = VK_NULL_HANDLE;
	m_queue = VK_NULL_HANDLE;
	m_queueFamilyIndex = ~0u;

	m_swapchainFormat = VK_FORMAT_UNDEFINED;
	memset( &m_swapchainExtent, 0, sizeof( m_swapchainExtent ) );
	m_swapchainTextures = NULL;
	m_swapchainImageCount = 0;
	m_currentSwapchainImage = 0;

	m_depthBuffer = NULL;

	m_renderPass = VK_NULL_HANDLE;

	memset( m_frames, 0, sizeof( m_frames ) );
	m_currentFrame = 0;

	m_commandPool = VK_NULL_HANDLE;
	m_uploadCommandPool = VK_NULL_HANDLE;
	m_activeCommandBuffer = VK_NULL_HANDLE;
	m_activeUploadBuffer = VK_NULL_HANDLE;
	m_inRenderPass = false;

	m_pipelineCache = VK_NULL_HANDLE;

	m_samplerDescriptorSetLayout = VK_NULL_HANDLE;
	m_pipelineLayout = VK_NULL_HANDLE;
	m_descriptorPool = VK_NULL_HANDLE;

	memset( m_samplers, 0, sizeof( m_samplers ) );
	for ( int i = 0; i < VK_SAMPLER_COUNT; ++i )
	{
		m_samplerParams[i].m_minFilter = VK_FILTER_LINEAR;
		m_samplerParams[i].m_magFilter = VK_FILTER_LINEAR;
		m_samplerParams[i].m_mipFilter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		m_samplerParams[i].m_addressU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		m_samplerParams[i].m_addressV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		m_samplerParams[i].m_addressW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		m_samplerParams[i].m_mipLodBias = 0.0f;
		m_samplerParams[i].m_minLod = 0.0f;
		m_samplerParams[i].m_maxLod = 1000.0f;
		m_samplerParams[i].m_maxAnisotropy = 1.0f;
		m_samplerParams[i].m_compareOp = VK_COMPARE_OP_NEVER;
		m_samplerParams[i].m_srgb = false;
		m_samplerParams[i].m_borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		m_samplerTexs[i] = NULL;
		m_samplerDirty[i] = true;
	}

	memset( m_boundRenderTargets, 0, sizeof( m_boundRenderTargets ) );
	m_boundDepthStencil = NULL;
	m_boundVertexShader = NULL;
	m_boundPixelShader = NULL;

	memset( &m_state, 0, sizeof( m_state ) );
	m_stateDirty = true;

	m_nTotalDrawsOrClears = 0;
	m_nTotalVBLockBytes = 0;
	m_nTotalIBLockBytes = 0;

	m_deviceLocalMemoryIndex = -1;
	m_hostVisibleMemoryIndex = -1;
	m_hostCoherentMemoryIndex = -1;

	m_ownerThreadId = 0;
}

CVKContext::~CVKContext()
{
	Destroy();
}

//-----------------------------------------------------------------------------
// FindMemoryType: linear scan of VkPhysicalDeviceMemoryProperties.
//-----------------------------------------------------------------------------
int32 CVKContext::FindMemoryType( uint32 typeBits, VkMemoryPropertyFlags properties )
{
	if ( !gVK || !gVK->vkGetPhysicalDeviceMemoryProperties )
	{
		return -1;
	}

	VkPhysicalDeviceMemoryProperties memProps;
	memset( &memProps, 0, sizeof( memProps ) );
	gVK->vkGetPhysicalDeviceMemoryProperties( gVK->m_physicalDevice, &memProps );

	for ( uint32_t i = 0; i < memProps.memoryTypeCount; ++i )
	{
		if ( ( typeBits & ( 1u << i ) ) &&
			 ( memProps.memoryTypes[i].propertyFlags & properties ) == properties )
		{
			return (int32)i;
		}
	}

	Warning( "CVKContext::FindMemoryType: no matching memory type (typeBits=0x%x props=0x%x)\n",
			 (unsigned)typeBits, (unsigned)properties );
	return -1;
}

//-----------------------------------------------------------------------------
// AllocateMemory: convenience wrapper around vkAllocateMemory.
//-----------------------------------------------------------------------------
VkDeviceMemory CVKContext::AllocateMemory( VkDeviceSize size, uint32 typeBits, VkMemoryPropertyFlags properties )
{
	int32 typeIndex = FindMemoryType( typeBits, properties );
	if ( typeIndex < 0 )
	{
		return VK_NULL_HANDLE;
	}

	VkMemoryAllocateInfo allocInfo;
	memset( &allocInfo, 0, sizeof( allocInfo ) );
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = size;
	allocInfo.memoryTypeIndex = (uint32_t)typeIndex;

	VkDeviceMemory memory = VK_NULL_HANDLE;
	if ( gVK->vkAllocateMemory( gVK->m_device, &allocInfo, VK_NULL_HANDLE, &memory ) != VK_SUCCESS )
	{
		Warning( "CVKContext::AllocateMemory: vkAllocateMemory failed (size=%llu)\n", (unsigned long long)size );
		return VK_NULL_HANDLE;
	}
	return memory;
}

//-----------------------------------------------------------------------------
// Create: full device-side bring-up.
//-----------------------------------------------------------------------------
bool CVKContext::Create( CVKDisplayParams *params )
{
	Assert( gVK && gVK->m_device != VK_NULL_HANDLE );
	if ( !gVK || gVK->m_device == VK_NULL_HANDLE )
	{
		Warning( "CVKContext::Create: no VkDevice available\n" );
		return false;
	}

	if ( params )
	{
		m_params = *params;
	}

	m_ownerThreadId = ThreadGetCurrentId();

	// Cache useful memory type indices for the device.
	VkPhysicalDeviceMemoryProperties memProps;
	memset( &memProps, 0, sizeof( memProps ) );
	gVK->vkGetPhysicalDeviceMemoryProperties( gVK->m_physicalDevice, &memProps );

	for ( uint32_t i = 0; i < memProps.memoryTypeCount; ++i )
	{
		VkMemoryPropertyFlags flags = memProps.memoryTypes[i].propertyFlags;
		if ( ( m_deviceLocalMemoryIndex < 0 ) && ( flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) )
		{
			m_deviceLocalMemoryIndex = (int32)i;
		}
		if ( ( m_hostVisibleMemoryIndex < 0 ) && ( flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) )
		{
			m_hostVisibleMemoryIndex = (int32)i;
		}
		if ( ( m_hostCoherentMemoryIndex < 0 ) &&
			 ( ( flags & ( VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) )
			   == ( VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) ) )
		{
			m_hostCoherentMemoryIndex = (int32)i;
		}
	}

	// Grab the graphics queue - re-derive the family index by querying again.
	uint32_t queueFamilyCount = 0;
	gVK->vkGetPhysicalDeviceQueueFamilyProperties( gVK->m_physicalDevice, &queueFamilyCount, NULL );
	if ( queueFamilyCount == 0 )
	{
		Warning( "CVKContext::Create: no queue families\n" );
		return false;
	}

	CUtlVector<VkQueueFamilyProperties> queueFamilies;
	queueFamilies.SetCount( queueFamilyCount );
	gVK->vkGetPhysicalDeviceQueueFamilyProperties( gVK->m_physicalDevice, &queueFamilyCount, queueFamilies.Base() );

	m_queueFamilyIndex = ~0u;
	for ( uint32_t q = 0; q < queueFamilyCount; ++q )
	{
		if ( queueFamilies[q].queueFlags & VK_QUEUE_GRAPHICS_BIT )
		{
			m_queueFamilyIndex = q;
			break;
		}
	}
	if ( m_queueFamilyIndex == ~0u )
	{
		Warning( "CVKContext::Create: no graphics queue family\n" );
		return false;
	}
	gVK->vkGetDeviceQueue( gVK->m_device, m_queueFamilyIndex, 0, &m_queue );

	// Create the surface before the swapchain so we can match formats.
#if defined( USE_SDL )
	if ( m_params.m_focusWindow )
	{
		SDL_Window *pWindow = (SDL_Window *)m_params.m_focusWindow;
		if ( !SDL_Vulkan_CreateSurface( pWindow, gVK->m_instance, &m_surface ) )
		{
			Warning( "CVKContext::Create: SDL_Vulkan_CreateSurface failed\n" );
			m_surface = VK_NULL_HANDLE;
		}
	}
#elif defined( __ANDROID__ )
	if ( m_params.m_focusWindow )
	{
		VkAndroidSurfaceCreateInfoKHR surfaceInfo;
		memset( &surfaceInfo, 0, sizeof( surfaceInfo ) );
		surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
		surfaceInfo.window = (ANativeWindow *)m_params.m_focusWindow;
		PFN_vkCreateAndroidSurfaceKHR pfnCreate =
			(PFN_vkCreateAndroidSurfaceKHR) VKGetInstanceProcAddr( "vkCreateAndroidSurfaceKHR" );
		if ( pfnCreate && pfnCreate( gVK->m_instance, &surfaceInfo, VK_NULL_HANDLE, &m_surface ) != VK_SUCCESS )
		{
			Warning( "CVKContext::Create: vkCreateAndroidSurfaceKHR failed\n" );
			m_surface = VK_NULL_HANDLE;
		}
	}
#elif !defined( _WIN32 )
	if ( m_params.m_focusWindow )
	{
		VkXlibSurfaceCreateInfoKHR surfaceInfo;
		memset( &surfaceInfo, 0, sizeof( surfaceInfo ) );
		surfaceInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
		// m_params.m_focusWindow is the Xlib Window handle (unsigned long);
		// dpy is left NULL (the SDL path is the common case, this is a fallback).
		surfaceInfo.window = (Window)(uintptr_t)m_params.m_focusWindow;
		PFN_vkCreateXlibSurfaceKHR pfnCreate =
			(PFN_vkCreateXlibSurfaceKHR) VKGetInstanceProcAddr( "vkCreateXlibSurfaceKHR" );
		if ( pfnCreate && pfnCreate( gVK->m_instance, &surfaceInfo, VK_NULL_HANDLE, &m_surface ) != VK_SUCCESS )
		{
			Warning( "CVKContext::Create: vkCreateXlibSurfaceKHR failed\n" );
			m_surface = VK_NULL_HANDLE;
		}
	}
#endif

	CreateRenderPass();
	CreateCommandPools();
	CreateFrameSync();
	CreateDescriptorSetLayouts();
	CreatePipelineLayout();
	CreateDescriptorPool();
	CreateSamplers();

	// Pipeline cache (empty at startup, persists for the life of the context).
	VkPipelineCacheCreateInfo cacheInfo;
	memset( &cacheInfo, 0, sizeof( cacheInfo ) );
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if ( gVK->vkCreatePipelineCache( gVK->m_device, &cacheInfo, VK_NULL_HANDLE, &m_pipelineCache ) != VK_SUCCESS )
	{
		Warning( "CVKContext::Create: vkCreatePipelineCache failed\n" );
		m_pipelineCache = VK_NULL_HANDLE;
	}

	// Swapchain last - it depends on the surface + render pass existing.
	if ( m_surface != VK_NULL_HANDLE )
	{
		if ( !CreateSwapchain() )
		{
			Warning( "CVKContext::Create: CreateSwapchain failed (continuing headless)\n" );
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Destroy: tear everything down in reverse dependency order.
//-----------------------------------------------------------------------------
void CVKContext::Destroy()
{
	if ( !gVK || gVK->m_device == VK_NULL_HANDLE )
	{
		return;
	}

	gVK->vkDeviceWaitIdle( gVK->m_device );

	DestroySwapchain();

	m_fboMap.DestroyAll();

	DestroySamplers();

	if ( m_descriptorPool != VK_NULL_HANDLE )
	{
		gVK->vkDestroyDescriptorPool( gVK->m_device, m_descriptorPool, VK_NULL_HANDLE );
		m_descriptorPool = VK_NULL_HANDLE;
	}

	if ( m_pipelineLayout != VK_NULL_HANDLE )
	{
		gVK->vkDestroyPipelineLayout( gVK->m_device, m_pipelineLayout, VK_NULL_HANDLE );
		m_pipelineLayout = VK_NULL_HANDLE;
	}

	if ( m_samplerDescriptorSetLayout != VK_NULL_HANDLE )
	{
		gVK->vkDestroyDescriptorSetLayout( gVK->m_device, m_samplerDescriptorSetLayout, VK_NULL_HANDLE );
		m_samplerDescriptorSetLayout = VK_NULL_HANDLE;
	}

	DestroyFrameSync();

	if ( m_pipelineCache != VK_NULL_HANDLE )
	{
		gVK->vkDestroyPipelineCache( gVK->m_device, m_pipelineCache, VK_NULL_HANDLE );
		m_pipelineCache = VK_NULL_HANDLE;
	}

	if ( m_renderPass != VK_NULL_HANDLE )
	{
		gVK->vkDestroyRenderPass( gVK->m_device, m_renderPass, VK_NULL_HANDLE );
		m_renderPass = VK_NULL_HANDLE;
	}

	if ( m_uploadCommandPool != VK_NULL_HANDLE )
	{
		gVK->vkDestroyCommandPool( gVK->m_device, m_uploadCommandPool, VK_NULL_HANDLE );
		m_uploadCommandPool = VK_NULL_HANDLE;
	}

	if ( m_commandPool != VK_NULL_HANDLE )
	{
		gVK->vkDestroyCommandPool( gVK->m_device, m_commandPool, VK_NULL_HANDLE );
		m_commandPool = VK_NULL_HANDLE;
	}

	if ( m_surface != VK_NULL_HANDLE && gVK->vkDestroySurfaceKHR )
	{
		gVK->vkDestroySurfaceKHR( gVK->m_instance, m_surface, VK_NULL_HANDLE );
		m_surface = VK_NULL_HANDLE;
	}

	m_queue = VK_NULL_HANDLE;
	m_queueFamilyIndex = ~0u;
}

//-----------------------------------------------------------------------------
// Display size accessors.
//-----------------------------------------------------------------------------
void CVKContext::GetDisplaySize( uint &width, uint &height )
{
	width = m_swapchainExtent.width;
	height = m_swapchainExtent.height;
}

void CVKContext::SetDisplaySize( uint width, uint height )
{
	m_params.m_backBufferWidth = width;
	m_params.m_backBufferHeight = height;

	// Recreate the swapchain at the new size.
	if ( m_swapchain != VK_NULL_HANDLE )
	{
		gVK->vkDeviceWaitIdle( gVK->m_device );
		DestroySwapchain();
		CreateSwapchain();
	}
}

//-----------------------------------------------------------------------------
// CreateRenderPass: a single color + optional depth attachment, matching the
// swapchain format. Used as the render pass for all default framebuffers.
//-----------------------------------------------------------------------------
void CVKContext::CreateRenderPass()
{
	VkAttachmentDescription attachments[2];
	memset( attachments, 0, sizeof( attachments ) );

	// Color attachment - matches swapchain format if known, else default B8G8R8A8.
	VkFormat colorFormat = ( m_swapchainFormat != VK_FORMAT_UNDEFINED ) ? m_swapchainFormat : VK_FORMAT_B8G8R8A8_UNORM;

	attachments[0].format = colorFormat;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorRef;
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass;
	memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	VkRenderPassCreateInfo rpInfo;
	memset( &rpInfo, 0, sizeof( rpInfo ) );
	rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpInfo.attachmentCount = 1;
	rpInfo.pAttachments = attachments;
	rpInfo.subpassCount = 1;
	rpInfo.pSubpasses = &subpass;

	if ( gVK->vkCreateRenderPass( gVK->m_device, &rpInfo, VK_NULL_HANDLE, &m_renderPass ) != VK_SUCCESS )
	{
		Warning( "CVKContext::CreateRenderPass: vkCreateRenderPass failed\n" );
		m_renderPass = VK_NULL_HANDLE;
	}
}

//-----------------------------------------------------------------------------
// CreateCommandPools: one pool for the per-frame graphics command buffers
// and a separate one for upload / transfer command buffers.
//-----------------------------------------------------------------------------
void CVKContext::CreateCommandPools()
{
	VkCommandPoolCreateInfo poolInfo;
	memset( &poolInfo, 0, sizeof( poolInfo ) );
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = m_queueFamilyIndex;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	if ( gVK->vkCreateCommandPool( gVK->m_device, &poolInfo, VK_NULL_HANDLE, &m_commandPool ) != VK_SUCCESS )
	{
		Warning( "CVKContext::CreateCommandPools: vkCreateCommandPool (graphics) failed\n" );
		m_commandPool = VK_NULL_HANDLE;
	}

	if ( gVK->vkCreateCommandPool( gVK->m_device, &poolInfo, VK_NULL_HANDLE, &m_uploadCommandPool ) != VK_SUCCESS )
	{
		Warning( "CVKContext::CreateCommandPools: vkCreateCommandPool (upload) failed\n" );
		m_uploadCommandPool = VK_NULL_HANDLE;
	}
}

//-----------------------------------------------------------------------------
// CreateFrameSync: per-frame fence + image-available / render-finished
// semaphores and a primary command buffer.
//-----------------------------------------------------------------------------
void CVKContext::CreateFrameSync()
{
	VkFenceCreateInfo fenceInfo;
	memset( &fenceInfo, 0, sizeof( fenceInfo ) );
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkSemaphoreCreateInfo semInfo;
	memset( &semInfo, 0, sizeof( semInfo ) );
	semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkCommandBufferAllocateInfo cmdInfo;
	memset( &cmdInfo, 0, sizeof( cmdInfo ) );
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdInfo.commandPool = m_commandPool;
	cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdInfo.commandBufferCount = 1;

	VkCommandBufferAllocateInfo uploadCmdInfo;
	memset( &uploadCmdInfo, 0, sizeof( uploadCmdInfo ) );
	uploadCmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	uploadCmdInfo.commandPool = m_uploadCommandPool;
	uploadCmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	uploadCmdInfo.commandBufferCount = 1;

	for ( uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i )
	{
		VKFrameSync &frame = m_frames[i];
		frame.m_renderFence = VK_NULL_HANDLE;
		frame.m_imageAvailableSemaphore = VK_NULL_HANDLE;
		frame.m_renderFinishedSemaphore = VK_NULL_HANDLE;
		frame.m_commandBuffer = VK_NULL_HANDLE;
		frame.m_uploadCommandBuffer = VK_NULL_HANDLE;
		frame.m_commandBufferBegan = false;
		frame.m_uploadBufferBegan = false;

		gVK->vkCreateFence( gVK->m_device, &fenceInfo, VK_NULL_HANDLE, &frame.m_renderFence );
		gVK->vkCreateSemaphore( gVK->m_device, &semInfo, VK_NULL_HANDLE, &frame.m_imageAvailableSemaphore );
		gVK->vkCreateSemaphore( gVK->m_device, &semInfo, VK_NULL_HANDLE, &frame.m_renderFinishedSemaphore );

		if ( m_commandPool != VK_NULL_HANDLE )
		{
			gVK->vkAllocateCommandBuffers( gVK->m_device, &cmdInfo, &frame.m_commandBuffer );
		}
		if ( m_uploadCommandPool != VK_NULL_HANDLE )
		{
			gVK->vkAllocateCommandBuffers( gVK->m_device, &uploadCmdInfo, &frame.m_uploadCommandBuffer );
		}
	}
}

//-----------------------------------------------------------------------------
// DestroyFrameSync.
//-----------------------------------------------------------------------------
void CVKContext::DestroyFrameSync()
{
	for ( uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i )
	{
		VKFrameSync &frame = m_frames[i];

		if ( frame.m_commandBuffer != VK_NULL_HANDLE && m_commandPool != VK_NULL_HANDLE )
		{
			gVK->vkFreeCommandBuffers( gVK->m_device, m_commandPool, 1, &frame.m_commandBuffer );
			frame.m_commandBuffer = VK_NULL_HANDLE;
		}

		if ( frame.m_uploadCommandBuffer != VK_NULL_HANDLE && m_uploadCommandPool != VK_NULL_HANDLE )
		{
			gVK->vkFreeCommandBuffers( gVK->m_device, m_uploadCommandPool, 1, &frame.m_uploadCommandBuffer );
			frame.m_uploadCommandBuffer = VK_NULL_HANDLE;
		}

		if ( frame.m_renderFence != VK_NULL_HANDLE )
		{
			gVK->vkDestroyFence( gVK->m_device, frame.m_renderFence, VK_NULL_HANDLE );
			frame.m_renderFence = VK_NULL_HANDLE;
		}

		if ( frame.m_imageAvailableSemaphore != VK_NULL_HANDLE )
		{
			gVK->vkDestroySemaphore( gVK->m_device, frame.m_imageAvailableSemaphore, VK_NULL_HANDLE );
			frame.m_imageAvailableSemaphore = VK_NULL_HANDLE;
		}

		if ( frame.m_renderFinishedSemaphore != VK_NULL_HANDLE )
		{
			gVK->vkDestroySemaphore( gVK->m_device, frame.m_renderFinishedSemaphore, VK_NULL_HANDLE );
			frame.m_renderFinishedSemaphore = VK_NULL_HANDLE;
		}
	}
}

//-----------------------------------------------------------------------------
// CreateDescriptorSetLayouts: one combined sampler/image layout with
// VK_SAMPLER_COUNT bindings.
//-----------------------------------------------------------------------------
void CVKContext::CreateDescriptorSetLayouts()
{
	VkDescriptorSetLayoutBinding bindings[VK_SAMPLER_COUNT];
	memset( bindings, 0, sizeof( bindings ) );
	for ( int i = 0; i < VK_SAMPLER_COUNT; ++i )
	{
		bindings[i].binding = i;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
		bindings[i].pImmutableSamplers = NULL;
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo;
	memset( &layoutInfo, 0, sizeof( layoutInfo ) );
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = VK_SAMPLER_COUNT;
	layoutInfo.pBindings = bindings;

	if ( gVK->vkCreateDescriptorSetLayout( gVK->m_device, &layoutInfo, VK_NULL_HANDLE, &m_samplerDescriptorSetLayout ) != VK_SUCCESS )
	{
		Warning( "CVKContext::CreateDescriptorSetLayouts: vkCreateDescriptorSetLayout failed\n" );
		m_samplerDescriptorSetLayout = VK_NULL_HANDLE;
	}
}

//-----------------------------------------------------------------------------
// CreatePipelineLayout: sampler set + a small push-constants range for
// constant-buffer / vertex data offsets.
//-----------------------------------------------------------------------------
void CVKContext::CreatePipelineLayout()
{
	VkPushConstantRange pushRange;
	memset( &pushRange, 0, sizeof( pushRange ) );
	pushRange.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
	pushRange.offset = 0;
	pushRange.size = 64; // small reserved block for fast constants

	VkDescriptorSetLayout setLayouts[1] = { m_samplerDescriptorSetLayout };

	VkPipelineLayoutCreateInfo layoutInfo;
	memset( &layoutInfo, 0, sizeof( layoutInfo ) );
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = ( m_samplerDescriptorSetLayout != VK_NULL_HANDLE ) ? 1 : 0;
	layoutInfo.pSetLayouts = setLayouts;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushRange;

	if ( gVK->vkCreatePipelineLayout( gVK->m_device, &layoutInfo, VK_NULL_HANDLE, &m_pipelineLayout ) != VK_SUCCESS )
	{
		Warning( "CVKContext::CreatePipelineLayout: vkCreatePipelineLayout failed\n" );
		m_pipelineLayout = VK_NULL_HANDLE;
	}
}

//-----------------------------------------------------------------------------
// CreateDescriptorPool: a generous pool sized for many combined image samplers.
//-----------------------------------------------------------------------------
void CVKContext::CreateDescriptorPool()
{
	VkDescriptorPoolSize poolSize;
	memset( &poolSize, 0, sizeof( poolSize ) );
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = VK_SAMPLER_COUNT * 64;

	VkDescriptorPoolCreateInfo poolInfo;
	memset( &poolInfo, 0, sizeof( poolInfo ) );
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 64;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;

	if ( gVK->vkCreateDescriptorPool( gVK->m_device, &poolInfo, VK_NULL_HANDLE, &m_descriptorPool ) != VK_SUCCESS )
	{
		Warning( "CVKContext::CreateDescriptorPool: vkCreateDescriptorPool failed\n" );
		m_descriptorPool = VK_NULL_HANDLE;
	}
}

//-----------------------------------------------------------------------------
// CreateSamplers / DestroySamplers: lazily (re)created samplers per slot.
//-----------------------------------------------------------------------------
void CVKContext::CreateSamplers()
{
	// Samplers are created on demand when first bound - just mark slots dirty.
	for ( int i = 0; i < VK_SAMPLER_COUNT; ++i )
	{
		m_samplerDirty[i] = true;
	}
}

void CVKContext::DestroySamplers()
{
	for ( int i = 0; i < VK_SAMPLER_COUNT; ++i )
	{
		if ( m_samplers[i] != VK_NULL_HANDLE )
		{
			gVK->vkDestroySampler( gVK->m_device, m_samplers[i], VK_NULL_HANDLE );
			m_samplers[i] = VK_NULL_HANDLE;
		}
	}
}

//-----------------------------------------------------------------------------
// CreateSwapchain.
//-----------------------------------------------------------------------------
bool CVKContext::CreateSwapchain()
{
	if ( m_surface == VK_NULL_HANDLE )
	{
		return false;
	}

	VkSurfaceCapabilitiesKHR caps;
	memset( &caps, 0, sizeof( caps ) );
	if ( gVK->vkGetPhysicalDeviceSurfaceCapabilitiesKHR( gVK->m_physicalDevice, m_surface, &caps ) != VK_SUCCESS )
	{
		Warning( "CVKContext::CreateSwapchain: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed\n" );
		return false;
	}

	uint32_t formatCount = 0;
	gVK->vkGetPhysicalDeviceSurfaceFormatsKHR( gVK->m_physicalDevice, m_surface, &formatCount, NULL );
	if ( formatCount == 0 )
	{
		Warning( "CVKContext::CreateSwapchain: surface has no formats\n" );
		return false;
	}

	CUtlVector<VkSurfaceFormatKHR> surfaceFormats;
	surfaceFormats.SetCount( formatCount );
	gVK->vkGetPhysicalDeviceSurfaceFormatsKHR( gVK->m_physicalDevice, m_surface, &formatCount, surfaceFormats.Base() );

	VkSurfaceFormatKHR chosenFormat = surfaceFormats[0];
	for ( uint32_t i = 0; i < formatCount; ++i )
	{
		if ( surfaceFormats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
			 surfaceFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR )
		{
			chosenFormat = surfaceFormats[i];
			break;
		}
	}

	m_swapchainFormat = chosenFormat.format;
	m_swapchainExtent = caps.currentExtent;
	if ( m_swapchainExtent.width == 0xFFFFFFFFu || m_swapchainExtent.height == 0xFFFFFFFFu )
	{
		m_swapchainExtent.width = m_params.m_backBufferWidth;
		m_swapchainExtent.height = m_params.m_backBufferHeight;
	}

	VkSwapchainCreateInfoKHR swapInfo;
	memset( &swapInfo, 0, sizeof( swapInfo ) );
	swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapInfo.surface = m_surface;
	swapInfo.minImageCount = ( caps.minImageCount + 1 <= caps.maxImageCount ) ? caps.minImageCount + 1 : caps.minImageCount;
	if ( caps.maxImageCount > 0 && swapInfo.minImageCount > caps.maxImageCount )
	{
		swapInfo.minImageCount = caps.maxImageCount;
	}
	swapInfo.imageFormat = chosenFormat.format;
	swapInfo.imageColorSpace = chosenFormat.colorSpace;
	swapInfo.imageExtent = m_swapchainExtent;
	swapInfo.imageArrayLayers = 1;
	swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapInfo.preTransform = caps.currentTransform;
	swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync on by default
	swapInfo.clipped = VK_TRUE;
	swapInfo.oldSwapchain = VK_NULL_HANDLE;

	if ( gVK->vkCreateSwapchainKHR( gVK->m_device, &swapInfo, VK_NULL_HANDLE, &m_swapchain ) != VK_SUCCESS )
	{
		Warning( "CVKContext::CreateSwapchain: vkCreateSwapchainKHR failed\n" );
		m_swapchain = VK_NULL_HANDLE;
		return false;
	}

	gVK->vkGetSwapchainImagesKHR( gVK->m_device, m_swapchain, &m_swapchainImageCount, NULL );
	if ( m_swapchainImageCount == 0 )
	{
		return false;
	}

	CUtlVector<VkImage> swapchainImages;
	swapchainImages.SetCount( m_swapchainImageCount );
	gVK->vkGetSwapchainImagesKHR( gVK->m_device, m_swapchain, &m_swapchainImageCount, swapchainImages.Base() );

	m_swapchainTextures = new CVKTex * [m_swapchainImageCount];
	for ( uint32 i = 0; i < m_swapchainImageCount; ++i )
	{
		CVKTexParams texParams;
		texParams.m_texType = kVKTex2D;
		texParams.m_format = m_swapchainFormat;
		texParams.m_width = m_swapchainExtent.width;
		texParams.m_height = m_swapchainExtent.height;
		texParams.m_isRenderTarget = true;

		// Allocate a CVKTex wrapper for this swapchain image. A full
		// implementation would adopt swapchainImages[i] into the wrapper
		// rather than allocating a new VkImage; CVKTex::Create allocates its
		// own image, so we skip Create here and leave the wrapper as a
		// placeholder keyed by the swapchain format/extent. Present() and
		// the swapchain destroy path both handle a wrapper with no image.
		CVKTex *tex = new CVKTex();
		(void)texParams;
		(void)swapchainImages[i];
		m_swapchainTextures[i] = tex;
	}

	return true;
}

//-----------------------------------------------------------------------------
// DestroySwapchain.
//-----------------------------------------------------------------------------
void CVKContext::DestroySwapchain()
{
	if ( m_swapchainTextures )
	{
		for ( uint32 i = 0; i < m_swapchainImageCount; ++i )
		{
			if ( m_swapchainTextures[i] )
			{
				m_swapchainTextures[i]->Destroy();
				delete m_swapchainTextures[i];
				m_swapchainTextures[i] = NULL;
			}
		}
		delete [] m_swapchainTextures;
		m_swapchainTextures = NULL;
	}
	m_swapchainImageCount = 0;

	if ( m_depthBuffer )
	{
		m_depthBuffer->Destroy();
		delete m_depthBuffer;
		m_depthBuffer = NULL;
	}

	if ( m_swapchain != VK_NULL_HANDLE )
	{
		gVK->vkDestroySwapchainKHR( gVK->m_device, m_swapchain, VK_NULL_HANDLE );
		m_swapchain = VK_NULL_HANDLE;
	}
}

//-----------------------------------------------------------------------------
// Present: acquire -> (caller fills cmd) -> submit -> present.
//-----------------------------------------------------------------------------
bool CVKContext::Present()
{
	if ( m_swapchain == VK_NULL_HANDLE )
	{
		return false;
	}

	VKFrameSync &frame = m_frames[m_currentFrame];

	// Wait for the previous frame using this slot to finish.
	gVK->vkWaitForFences( gVK->m_device, 1, &frame.m_renderFence, VK_TRUE, UINT64_MAX );
	gVK->vkResetFences( gVK->m_device, 1, &frame.m_renderFence );

	VkResult acquireResult = gVK->vkAcquireNextImageKHR(
		gVK->m_device, m_swapchain, UINT64_MAX,
		frame.m_imageAvailableSemaphore, VK_NULL_HANDLE, &m_currentSwapchainImage );

	if ( acquireResult == VK_ERROR_OUT_OF_DATE_KHR )
	{
		DestroySwapchain();
		CreateSwapchain();
		return false;
	}
	if ( acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR )
	{
		Warning( "CVKContext::Present: vkAcquireNextImageKHR failed (%d)\n", (int)acquireResult );
		return false;
	}

	// End any command buffer the caller left open.
	if ( frame.m_commandBufferBegan )
	{
		gVK->vkEndCommandBuffer( frame.m_commandBuffer );
		frame.m_commandBufferBegan = false;
	}

	VkCommandBuffer cmdBuffers[1] = { frame.m_commandBuffer };

	VkSubmitInfo submitInfo;
	memset( &submitInfo, 0, sizeof( submitInfo ) );
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &frame.m_imageAvailableSemaphore;
	submitInfo.pWaitDstStageMask = &waitStage;
	submitInfo.commandBufferCount = ( frame.m_commandBuffer != VK_NULL_HANDLE ) ? 1 : 0;
	submitInfo.pCommandBuffers = cmdBuffers;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &frame.m_renderFinishedSemaphore;

	if ( gVK->vkQueueSubmit( m_queue, 1, &submitInfo, frame.m_renderFence ) != VK_SUCCESS )
	{
		Warning( "CVKContext::Present: vkQueueSubmit failed\n" );
		return false;
	}

	VkPresentInfoKHR presentInfo;
	memset( &presentInfo, 0, sizeof( presentInfo ) );
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &frame.m_renderFinishedSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &m_swapchain;
	presentInfo.pImageIndices = &m_currentSwapchainImage;

	VkResult presentResult = gVK->vkQueuePresentKHR( m_queue, &presentInfo );
	if ( presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR )
	{
		DestroySwapchain();
		CreateSwapchain();
	}

	m_currentFrame = ( m_currentFrame + 1 ) % MAX_FRAMES_IN_FLIGHT;
	m_nTotalDrawsOrClears = 0;

	m_activeCommandBuffer = VK_NULL_HANDLE;
	m_inRenderPass = false;

	return true;
}

//-----------------------------------------------------------------------------
// GetCommandBuffer: ensure the current frame's command buffer is recording.
//-----------------------------------------------------------------------------
VkCommandBuffer CVKContext::GetCommandBuffer()
{
	VKFrameSync &frame = m_frames[m_currentFrame];
	if ( !frame.m_commandBufferBegan && frame.m_commandBuffer != VK_NULL_HANDLE )
	{
		VkCommandBufferBeginInfo beginInfo;
		memset( &beginInfo, 0, sizeof( beginInfo ) );
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		gVK->vkBeginCommandBuffer( frame.m_commandBuffer, &beginInfo );
		frame.m_commandBufferBegan = true;
	}

	m_activeCommandBuffer = frame.m_commandBuffer;
	return frame.m_commandBuffer;
}

//-----------------------------------------------------------------------------
// GetUploadCommandBuffer: a side-channel command buffer for staging uploads.
//-----------------------------------------------------------------------------
VkCommandBuffer CVKContext::GetUploadCommandBuffer()
{
	VKFrameSync &frame = m_frames[m_currentFrame];
	if ( !frame.m_uploadBufferBegan && frame.m_uploadCommandBuffer != VK_NULL_HANDLE )
	{
		VkCommandBufferBeginInfo beginInfo;
		memset( &beginInfo, 0, sizeof( beginInfo ) );
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		gVK->vkBeginCommandBuffer( frame.m_uploadCommandBuffer, &beginInfo );
		frame.m_uploadBufferBegan = true;
	}

	m_activeUploadBuffer = frame.m_uploadCommandBuffer;
	return frame.m_uploadCommandBuffer;
}

//-----------------------------------------------------------------------------
// FlushCommandBuffers: end + submit the per-frame command buffers, wait on
// the render fence so callers know the GPU is idle afterwards.
//-----------------------------------------------------------------------------
void CVKContext::FlushCommandBuffers()
{
	VKFrameSync &frame = m_frames[m_currentFrame];

	if ( m_inRenderPass && frame.m_commandBuffer != VK_NULL_HANDLE )
	{
		gVK->vkCmdEndRenderPass( frame.m_commandBuffer );
		m_inRenderPass = false;
	}

	if ( frame.m_uploadBufferBegan )
	{
		gVK->vkEndCommandBuffer( frame.m_uploadCommandBuffer );
		frame.m_uploadBufferBegan = false;

		VkSubmitInfo uploadSubmit;
		memset( &uploadSubmit, 0, sizeof( uploadSubmit ) );
		uploadSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		uploadSubmit.commandBufferCount = 1;
		uploadSubmit.pCommandBuffers = &frame.m_uploadCommandBuffer;
		gVK->vkQueueSubmit( m_queue, 1, &uploadSubmit, VK_NULL_HANDLE );
	}

	if ( frame.m_commandBufferBegan )
	{
		gVK->vkEndCommandBuffer( frame.m_commandBuffer );
		frame.m_commandBufferBegan = false;

		VkSubmitInfo submitInfo;
		memset( &submitInfo, 0, sizeof( submitInfo ) );
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &frame.m_commandBuffer;

		gVK->vkResetFences( gVK->m_device, 1, &frame.m_renderFence );
		gVK->vkQueueSubmit( m_queue, 1, &submitInfo, frame.m_renderFence );
		gVK->vkWaitForFences( gVK->m_device, 1, &frame.m_renderFence, VK_TRUE, UINT64_MAX );
		gVK->vkResetCommandBuffer( frame.m_commandBuffer, 0 );
	}

	m_activeCommandBuffer = VK_NULL_HANDLE;
	m_activeUploadBuffer = VK_NULL_HANDLE;
}

//-----------------------------------------------------------------------------
// BeginRenderPass / EndRenderPass.
//-----------------------------------------------------------------------------
void CVKContext::BeginRenderPass()
{
	if ( m_inRenderPass )
	{
		return;
	}

	VkCommandBuffer cmd = GetCommandBuffer();
	if ( cmd == VK_NULL_HANDLE )
	{
		return;
	}

	RenderTargetState_t key;
	key.m_pRenderTargets[0] = m_boundRenderTargets[0];
	key.m_pDepthStencil = m_boundDepthStencil;

	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	if ( m_boundRenderTargets[0] || m_boundDepthStencil )
	{
		CVKFramebuffer *fbo = m_fboMap.FindOrCreate( key, this );
		if ( fbo )
		{
			framebuffer = fbo->GetFramebuffer();
		}
	}

	VkRenderPassBeginInfo rpBegin;
	memset( &rpBegin, 0, sizeof( rpBegin ) );
	rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBegin.renderPass = m_renderPass;
	rpBegin.framebuffer = framebuffer;
	rpBegin.renderArea.offset.x = 0;
	rpBegin.renderArea.offset.y = 0;
	rpBegin.renderArea.extent = m_swapchainExtent;
	rpBegin.clearValueCount = 0;
	rpBegin.pClearValues = NULL;

	gVK->vkCmdBeginRenderPass( cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );
	m_inRenderPass = true;
}

void CVKContext::EndRenderPass()
{
	if ( !m_inRenderPass )
	{
		return;
	}

	VkCommandBuffer cmd = m_frames[m_currentFrame].m_commandBuffer;
	if ( cmd != VK_NULL_HANDLE )
	{
		gVK->vkCmdEndRenderPass( cmd );
	}
	m_inRenderPass = false;
}

//-----------------------------------------------------------------------------
// Texture / buffer / shader / query factories.
//-----------------------------------------------------------------------------
CVKTex *CVKContext::CreateTex( const CVKTexParams &params )
{
	CVKTex *tex = new CVKTex();
	if ( !tex->Create( this, params ) )
	{
		delete tex;
		return NULL;
	}
	return tex;
}

void CVKContext::DestroyTex( CVKTex *tex )
{
	if ( tex )
	{
		tex->Destroy();
		delete tex;
	}
}

void CVKContext::SetSamplerTex( int sampler, CVKTex *tex )
{
	if ( sampler < 0 || sampler >= VK_SAMPLER_COUNT )
	{
		return;
	}
	m_samplerTexs[sampler] = tex;
	m_samplerDirty[sampler] = true;
}

void CVKContext::SetSamplerStates( int sampler, VkFilter minFilter, VkFilter magFilter, VkSamplerMipmapMode mipFilter,
	VkSamplerAddressMode addrU, VkSamplerAddressMode addrV, VkSamplerAddressMode addrW,
	int minLod, float lodBias )
{
	if ( sampler < 0 || sampler >= VK_SAMPLER_COUNT )
	{
		return;
	}
	VKTexSamplingParams &p = m_samplerParams[sampler];
	p.m_minFilter = minFilter;
	p.m_magFilter = magFilter;
	p.m_mipFilter = mipFilter;
	p.m_addressU = addrU;
	p.m_addressV = addrV;
	p.m_addressW = addrW;
	p.m_minLod = (float)minLod;
	p.m_mipLodBias = lodBias;
	m_samplerDirty[sampler] = true;
}

void CVKContext::SetSamplerMaxAnisotropy( int sampler, float value )
{
	if ( sampler < 0 || sampler >= VK_SAMPLER_COUNT )
	{
		return;
	}
	m_samplerParams[sampler].m_maxAnisotropy = value;
	m_samplerDirty[sampler] = true;
}

void CVKContext::SetSamplerSRGB( int sampler, bool value )
{
	if ( sampler < 0 || sampler >= VK_SAMPLER_COUNT )
	{
		return;
	}
	m_samplerParams[sampler].m_srgb = value;
	m_samplerDirty[sampler] = true;
}

CVKBuffer *CVKContext::CreateBuffer( VkBufferUsageFlags usage, VkDeviceSize size, bool dynamic )
{
	CVKBuffer *buf = new CVKBuffer();
	if ( !buf->Create( this, usage, size, dynamic ) )
	{
		delete buf;
		return NULL;
	}
	return buf;
}

void CVKContext::DestroyBuffer( CVKBuffer *buffer )
{
	if ( buffer )
	{
		buffer->Destroy();
		delete buffer;
	}
}

CVKProgram *CVKContext::CreateProgram( VKProgramType type, const void *data, size_t size )
{
	CVKProgram *prog = new CVKProgram();
	if ( !prog->Create( this, type, data, size ) )
	{
		delete prog;
		return NULL;
	}
	return prog;
}

void CVKContext::DestroyProgram( CVKProgram *program )
{
	if ( program )
	{
		program->Destroy();
		delete program;
	}
}

CVKQuery *CVKContext::CreateQuery( VkQueryType type )
{
	CVKQuery *query = new CVKQuery();
	if ( !query->Create( this, type ) )
	{
		delete query;
		return NULL;
	}
	return query;
}

void CVKContext::DestroyQuery( CVKQuery *query )
{
	if ( query )
	{
		query->Destroy();
		delete query;
	}
}

CVKFramebuffer *CVKContext::GetFBO( const RenderTargetState_t &key )
{
	return m_fboMap.FindOrCreate( key, this );
}

//-----------------------------------------------------------------------------
// Write* state-bucket updaters. Each marks m_stateDirty so FlushDrawStates
// re-applies the dynamic state before the next draw.
//-----------------------------------------------------------------------------
void CVKContext::WriteDepthTestEnable( bool enable ) { m_state.m_DepthTestEnable.value = enable; m_stateDirty = true; }
void CVKContext::WriteDepthMask( bool enable )       { m_state.m_DepthMask.value = enable; m_stateDirty = true; }
void CVKContext::WriteDepthFunc( VkCompareOp func )  { m_state.m_DepthFunc.value = func; m_stateDirty = true; }
void CVKContext::WriteDepthBias( float bias, float biasClamp, float slopeScaledBias )
{
	m_state.m_DepthBias.bias = bias;
	m_state.m_DepthBias.biasClamp = biasClamp;
	m_state.m_DepthBias.slopeScaledBias = slopeScaledBias;
	m_stateDirty = true;
}
void CVKContext::WriteCullFaceEnable( bool enable )       { m_state.m_CullFaceEnable.value = enable; m_stateDirty = true; }
void CVKContext::WriteCullFrontFace( VkCullModeFlags mode ){ m_state.m_CullFrontFace.value = mode; m_stateDirty = true; }
void CVKContext::WriteBlendEnable( int target, bool enable )
{
	if ( target >= 0 && target < MAX_RENDER_TARGETS )
	{
		m_state.m_BlendEnable.value[target] = enable;
		m_stateDirty = true;
	}
}
void CVKContext::WriteBlendFactor( int target, VkBlendFactor src, VkBlendFactor dst )
{
	if ( target >= 0 && target < MAX_RENDER_TARGETS )
	{
		m_state.m_BlendFactor.src = src;
		m_state.m_BlendFactor.dst = dst;
		m_stateDirty = true;
	}
}
void CVKContext::WriteBlendEquation( int target, VkBlendOp op )
{
	if ( target >= 0 && target < MAX_RENDER_TARGETS )
	{
		m_state.m_BlendEquation.op = op;
		m_stateDirty = true;
	}
}
void CVKContext::WriteBlendColor( float r, float g, float b, float a )
{
	m_state.m_BlendColor.r = r;
	m_state.m_BlendColor.g = g;
	m_state.m_BlendColor.b = b;
	m_state.m_BlendColor.a = a;
	m_stateDirty = true;
}
void CVKContext::WriteBlendEnableSRGB( bool enable ) { m_state.m_BlendEnableSRGB.value = enable; m_stateDirty = true; }
void CVKContext::WriteStencilTestEnable( bool enable ) { m_state.m_StencilTestEnable.value = enable; m_stateDirty = true; }
void CVKContext::WriteStencilFunc( VkCompareOp func, uint32 ref, uint32 mask )
{
	m_state.m_StencilFunc.func = func;
	m_state.m_StencilFunc.ref = ref;
	m_state.m_StencilFunc.mask = mask;
	m_stateDirty = true;
}
void CVKContext::WriteStencilOp( VkStencilOp sfail, VkStencilOp dpfail, VkStencilOp dppass )
{
	m_state.m_StencilOp.sfail = sfail;
	m_state.m_StencilOp.dpfail = dpfail;
	m_state.m_StencilOp.dppass = dppass;
	m_stateDirty = true;
}
void CVKContext::WriteStencilWriteMask( uint32 mask ) { m_state.m_StencilWriteMask.value = mask; m_stateDirty = true; }
void CVKContext::WriteColorMask( int target, uint8 mask )
{
	if ( target < 0 )
	{
		m_state.m_ColorMaskSingle.value = mask;
	}
	else if ( target < MAX_RENDER_TARGETS )
	{
		m_state.m_ColorMaskMultiple.value[target] = mask;
	}
	m_stateDirty = true;
}
void CVKContext::WriteViewport( const VkViewport &vp )
{
	m_state.m_ViewportBox.viewport = vp;
	m_stateDirty = true;
}
void CVKContext::WriteScissor( const VkRect2D &rect )
{
	m_state.m_ScissorBox.rect = rect;
	m_stateDirty = true;
}
void CVKContext::WriteScissorEnable( bool enable ) { m_state.m_ScissorEnable.value = enable; m_stateDirty = true; }
void CVKContext::WriteClipPlaneEnable( int idx, bool enable )
{
	if ( idx >= 0 && idx < kVKUserClipPlanes )
	{
		m_state.m_ClipPlaneEnable.value[idx] = enable;
		m_stateDirty = true;
	}
}
void CVKContext::WriteClipPlaneEquation( int idx, const float eq[4] )
{
	if ( idx >= 0 && idx < kVKUserClipPlanes )
	{
		memcpy( m_state.m_ClipPlaneEquation.value[idx], eq, sizeof( float ) * 4 );
		m_stateDirty = true;
	}
}

//-----------------------------------------------------------------------------
// FlushDrawStates: bind dynamic state + descriptor sets before a draw.
// In a full implementation this would also (re)build the VkPipeline via the
// pipeline cache; here we focus on the dynamic state commands which are
// always safe to issue.
//-----------------------------------------------------------------------------
void CVKContext::FlushDrawStates( uint nStartIndex, uint nEndIndex, uint nBaseVertex )
{
	VkCommandBuffer cmd = GetCommandBuffer();
	if ( cmd == VK_NULL_HANDLE )
	{
		return;
	}

	if ( !m_inRenderPass )
	{
		BeginRenderPass();
	}

	// Viewport / scissor are always dynamic in this backend.
	gVK->vkCmdSetViewport( cmd, 0, 1, &m_state.m_ViewportBox.viewport );
	if ( m_state.m_ScissorEnable.value )
	{
		gVK->vkCmdSetScissor( cmd, 0, 1, &m_state.m_ScissorBox.rect );
	}
	else
	{
		VkRect2D fullRect;
		fullRect.offset.x = 0;
		fullRect.offset.y = 0;
		fullRect.extent = m_swapchainExtent;
		gVK->vkCmdSetScissor( cmd, 0, 1, &fullRect );
	}

	// Blend constants.
	gVK->vkCmdSetBlendConstants( cmd, &m_state.m_BlendColor.r );

	// Stencil reference + write mask.
	gVK->vkCmdSetStencilReference( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, m_state.m_StencilFunc.ref );

	// Depth bias.
	if ( m_state.m_DepthBias.bias != 0.0f || m_state.m_DepthBias.slopeScaledBias != 0.0f )
	{
		gVK->vkCmdSetDepthBias( cmd, m_state.m_DepthBias.bias, m_state.m_DepthBias.biasClamp, m_state.m_DepthBias.slopeScaledBias );
	}

	m_stateDirty = false;
}

//-----------------------------------------------------------------------------
// DrawPrimitive / DrawIndexedPrimitive.
//-----------------------------------------------------------------------------
void CVKContext::DrawPrimitive( VkPrimitiveTopology topology, uint startVertex, uint vertexCount )
{
	VkCommandBuffer cmd = GetCommandBuffer();
	if ( cmd == VK_NULL_HANDLE )
	{
		return;
	}

	if ( m_stateDirty )
	{
		FlushDrawStates( 0, 0, 0 );
	}

	gVK->vkCmdDraw( cmd, vertexCount, 1, startVertex, 0 );
	m_nTotalDrawsOrClears++;
}

void CVKContext::DrawIndexedPrimitive( VkPrimitiveTopology topology, uint startIndex, uint indexCount, int baseVertex, uint baseIndex )
{
	VkCommandBuffer cmd = GetCommandBuffer();
	if ( cmd == VK_NULL_HANDLE )
	{
		return;
	}

	if ( m_stateDirty )
	{
		FlushDrawStates( 0, 0, (uint)baseVertex );
	}

	gVK->vkCmdDrawIndexed( cmd, indexCount, 1, startIndex, baseVertex, 0 );
	m_nTotalDrawsOrClears++;
}

//-----------------------------------------------------------------------------
// Clear: vkCmdClearAttachments inside a render pass, or vkCmdClearColorImage
// outside one.
//-----------------------------------------------------------------------------
void CVKContext::Clear( uint32 mask, const float *color, float depth, uint32 stencil )
{
	VkCommandBuffer cmd = GetCommandBuffer();
	if ( cmd == VK_NULL_HANDLE )
	{
		return;
	}

	if ( m_inRenderPass )
	{
		VkClearAttachment attachments[2];
		VkClearRect clearRects[2];
		int clearCount = 0;

		if ( mask & 0x1 /* D3DCLEAR_TARGET */ )
		{
			VkClearAttachment &att = attachments[clearCount];
			memset( &att, 0, sizeof( att ) );
			att.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			att.colorAttachment = 0;
			if ( color )
			{
				att.clearValue.color.float32[0] = color[0];
				att.clearValue.color.float32[1] = color[1];
				att.clearValue.color.float32[2] = color[2];
				att.clearValue.color.float32[3] = color[3];
			}

			VkClearRect &rect = clearRects[clearCount];
			memset( &rect, 0, sizeof( rect ) );
			rect.rect.offset.x = 0;
			rect.rect.offset.y = 0;
			rect.rect.extent = m_swapchainExtent;
			rect.baseArrayLayer = 0;
			rect.layerCount = 1;

			++clearCount;
		}

		if ( ( mask & 0x2 /* D3DCLEAR_ZBUFFER */ ) || ( mask & 0x4 /* D3DCLEAR_STENCIL */ ) )
		{
			VkClearAttachment &att = attachments[clearCount];
			memset( &att, 0, sizeof( att ) );
			att.aspectMask = 0;
			if ( mask & 0x2 ) att.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			if ( mask & 0x4 ) att.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			att.clearValue.depthStencil.depth = depth;
			att.clearValue.depthStencil.stencil = stencil;

			VkClearRect &rect = clearRects[clearCount];
			memset( &rect, 0, sizeof( rect ) );
			rect.rect.offset.x = 0;
			rect.rect.offset.y = 0;
			rect.rect.extent = m_swapchainExtent;
			rect.baseArrayLayer = 0;
			rect.layerCount = 1;

			++clearCount;
		}

		if ( clearCount > 0 )
		{
			gVK->vkCmdClearAttachments( cmd, clearCount, attachments, clearCount, clearRects );
		}
	}
	else
	{
		if ( ( mask & 0x1 ) && color && m_swapchainTextures && m_currentSwapchainImage < m_swapchainImageCount )
		{
			CVKTex *tex = m_swapchainTextures[m_currentSwapchainImage];
			if ( tex )
			{
				ClearColorImage( tex, color );
			}
		}
	}

	m_nTotalDrawsOrClears++;
}

void CVKContext::ClearColorImage( CVKTex *tex, const float color[4] )
{
	if ( !tex || !gVK->vkCmdClearColorImage )
	{
		return;
	}

	VkCommandBuffer cmd = GetCommandBuffer();
	if ( cmd == VK_NULL_HANDLE )
	{
		return;
	}

	VkClearColorValue clearValue;
	memcpy( clearValue.float32, color, sizeof( float ) * 4 );

	VkImageSubresourceRange range;
	memset( &range, 0, sizeof( range ) );
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	range.baseMipLevel = 0;
	range.levelCount = tex->GetMipCount();
	range.baseArrayLayer = 0;
	range.layerCount = tex->GetFaceCount();

	VkImageLayout oldLayout = tex->GetLayout();
	if ( oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL )
	{
		tex->TransitionLayout( cmd, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	}

	gVK->vkCmdClearColorImage( cmd, tex->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range );

	tex->TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, oldLayout );
}

void CVKContext::ClearDepthStencilImage( CVKTex *tex, float depth, uint32 stencil )
{
	if ( !tex || !gVK->vkCmdClearDepthStencilImage )
	{
		return;
	}

	VkCommandBuffer cmd = GetCommandBuffer();
	if ( cmd == VK_NULL_HANDLE )
	{
		return;
	}

	VkClearDepthStencilValue clearValue;
	clearValue.depth = depth;
	clearValue.stencil = stencil;

	VkImageSubresourceRange range;
	memset( &range, 0, sizeof( range ) );
	range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	range.baseMipLevel = 0;
	range.levelCount = tex->GetMipCount();
	range.baseArrayLayer = 0;
	range.layerCount = tex->GetFaceCount();

	VkImageLayout oldLayout = tex->GetLayout();
	if ( oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL )
	{
		tex->TransitionLayout( cmd, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	}

	gVK->vkCmdClearDepthStencilImage( cmd, tex->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range );

	tex->TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, oldLayout );
}

//-----------------------------------------------------------------------------
// BlitTex / CopyTex.
//-----------------------------------------------------------------------------
void CVKContext::BlitTex( CVKTex *src, CVKTex *dst, const VkImageBlit *blit )
{
	if ( !src || !dst || !blit || !gVK->vkCmdBlitImage )
	{
		return;
	}

	VkCommandBuffer cmd = GetCommandBuffer();
	if ( cmd == VK_NULL_HANDLE )
	{
		return;
	}

	VkImageLayout srcOld = src->GetLayout();
	VkImageLayout dstOld = dst->GetLayout();
	if ( srcOld != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL )
	{
		src->TransitionLayout( cmd, srcOld, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	}
	if ( dstOld != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL )
	{
		dst->TransitionLayout( cmd, dstOld, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	}

	VkFilter filter = VK_FILTER_LINEAR;
	gVK->vkCmdBlitImage( cmd, src->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						 dst->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, blit, filter );

	src->TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcOld );
	dst->TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dstOld );
}

void CVKContext::CopyTex( CVKTex *src, CVKTex *dst, const VkImageCopy *copy )
{
	if ( !src || !dst || !copy || !gVK->vkCmdCopyImage )
	{
		return;
	}

	VkCommandBuffer cmd = GetCommandBuffer();
	if ( cmd == VK_NULL_HANDLE )
	{
		return;
	}

	VkImageLayout srcOld = src->GetLayout();
	VkImageLayout dstOld = dst->GetLayout();
	if ( srcOld != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL )
	{
		src->TransitionLayout( cmd, srcOld, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	}
	if ( dstOld != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL )
	{
		dst->TransitionLayout( cmd, dstOld, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	}

	gVK->vkCmdCopyImage( cmd, src->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						dst->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, copy );

	src->TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcOld );
	dst->TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dstOld );
}

//-----------------------------------------------------------------------------
// SetGammaRamp: applied via the OS / window-system path, not the device.
//-----------------------------------------------------------------------------
void CVKContext::SetGammaRamp( const void *ramp )
{
	// Vulkan has no direct gamma ramp API; the SDL/window-system owns this.
	(void)ramp;
}
