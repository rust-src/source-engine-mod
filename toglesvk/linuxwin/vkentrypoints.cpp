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
// vkentrypoints.cpp
//
// Vulkan entry point loading and management. Mirrors glentrypoints.cpp from
// the GL backend: dlopen("libvulkan.so") on Linux/Android, then resolve every
// instance/device function pointer through vkGetInstanceProcAddr.
//
//=============================================================================
#include "toglesvk/rendermechanism.h"

#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include "tier0/vprof.h"
#include "tier0/platform.h"

#if !defined( _WIN32 )
	#include <dlfcn.h>
#endif

#ifdef USE_SDL
	#include "SDL.h"
	#include "SDL_vulkan.h"
#endif

// memdbgon -must- be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

#if !defined( DX_TO_VK_ABSTRACTION )
#error vkentrypoints.cpp must only be compiled under DX_TO_VK_ABSTRACTION
#endif

// Global entry points singleton (consumed everywhere via gVK->...).
CVulkanEntryPoints *gVK = NULL;

// Static vkGetInstanceProcAddr resolved out of the shared library before the
// CVulkanEntryPoints instance exists. Resolved by VKConnectLibraries() and used
// by VKGetInstanceProcAddr() / CVulkanEntryPoints::LoadInstanceFunctions().
static PFN_vkGetInstanceProcAddr s_vkGetInstanceProcAddr = NULL;

// File-static dlopen handle. CVulkanEntryPoints::m_vulkanLib is private in the
// public header, so VKConnectLibraries (a free function) tracks the handle
// here. The loader is intentionally left open for the lifetime of the process
// (the VkInstance / VkDevice outlive any single CVulkanEntryPoints object).
static void *s_vulkanLibHandle = NULL;

//-----------------------------------------------------------------------------
// Helper: resolve a name through the loaded vkGetInstanceProcAddr, logging
// failures for required entry points.
//-----------------------------------------------------------------------------
static void *VKLookupProc( const char *pName, bool bRequired )
{
	if ( !s_vkGetInstanceProcAddr )
	{
		if ( bRequired )
		{
			Warning( "VKLookupProc('%s'): vkGetInstanceProcAddr not available\n", pName );
		}
		return NULL;
	}

	void *p = (void *)s_vkGetInstanceProcAddr( VK_NULL_HANDLE, pName );
#ifdef USE_SDL
	if ( !p )
	{
		// SDL_Vulkan_GetInstanceProcAddress works without an instance for
		// bootstrap entry points; for instance-level entry points the path
		// above will already have succeeded.
		p = (void *)SDL_Vulkan_GetInstanceProcAddress( VK_NULL_HANDLE, pName );
	}
#endif
	if ( !p && bRequired )
	{
		Warning( "Could not find required Vulkan entry point '%s'!\n", pName );
	}
	return p;
}

//-----------------------------------------------------------------------------
// Public helper used by the rest of the backend.
//-----------------------------------------------------------------------------
PFN_vkVoidFunction VKGetInstanceProcAddr( const char *pName )
{
	if ( !s_vkGetInstanceProcAddr || !gVK || !gVK->m_instance )
	{
		return (PFN_vkVoidFunction)VKLookupProc( pName, false );
	}
	return s_vkGetInstanceProcAddr( gVK->m_instance, pName );
}

//=============================================================================
// CVulkanEntryPoints
//=============================================================================

CVulkanEntryPoints::CVulkanEntryPoints()
{
	m_instance = VK_NULL_HANDLE;
	m_device = VK_NULL_HANDLE;
	m_physicalDevice = VK_NULL_HANDLE;
	m_vulkanLib = NULL;

	m_nDriverProvider = cVKDriverProviderUnknown;
	m_pDriverStrings[0] = "";
	m_pDriverStrings[1] = "";
	m_pDriverStrings[2] = "";
	m_nApiVersionMajor = 0;
	m_nApiVersionMinor = 0;
	m_nApiVersionPatch = 0;

	// Zero every function pointer so a stale lookup is an obvious NULL deref.
#define VK_NULL_PTR( x )	x = NULL;

	VK_NULL_PTR( vkDestroyInstance );
	VK_NULL_PTR( vkEnumeratePhysicalDevices );
	VK_NULL_PTR( vkGetPhysicalDeviceProperties );
	VK_NULL_PTR( vkGetPhysicalDeviceFeatures );
	VK_NULL_PTR( vkGetPhysicalDeviceMemoryProperties );
	VK_NULL_PTR( vkGetPhysicalDeviceQueueFamilyProperties );
	VK_NULL_PTR( vkGetPhysicalDeviceFormatProperties );
	VK_NULL_PTR( vkEnumerateDeviceExtensionProperties );

	VK_NULL_PTR( vkCreateDevice );
	VK_NULL_PTR( vkDestroyDevice );
	VK_NULL_PTR( vkGetDeviceQueue );
	VK_NULL_PTR( vkDeviceWaitIdle );

	VK_NULL_PTR( vkQueueSubmit );
	VK_NULL_PTR( vkQueueWaitIdle );
	VK_NULL_PTR( vkAllocateCommandBuffers );
	VK_NULL_PTR( vkFreeCommandBuffers );
	VK_NULL_PTR( vkBeginCommandBuffer );
	VK_NULL_PTR( vkEndCommandBuffer );
	VK_NULL_PTR( vkResetCommandBuffer );

	VK_NULL_PTR( vkAllocateMemory );
	VK_NULL_PTR( vkFreeMemory );
	VK_NULL_PTR( vkMapMemory );
	VK_NULL_PTR( vkUnmapMemory );
	VK_NULL_PTR( vkFlushMappedMemoryRanges );
	VK_NULL_PTR( vkInvalidateMappedMemoryRanges );
	VK_NULL_PTR( vkBindBufferMemory );
	VK_NULL_PTR( vkBindImageMemory );
	VK_NULL_PTR( vkGetBufferMemoryRequirements );
	VK_NULL_PTR( vkGetImageMemoryRequirements );
	VK_NULL_PTR( vkGetImageSubresourceLayout );

	VK_NULL_PTR( vkCreateBuffer );
	VK_NULL_PTR( vkDestroyBuffer );

	VK_NULL_PTR( vkCreateImage );
	VK_NULL_PTR( vkDestroyImage );
	VK_NULL_PTR( vkCreateImageView );
	VK_NULL_PTR( vkDestroyImageView );

	VK_NULL_PTR( vkCreateSampler );
	VK_NULL_PTR( vkDestroySampler );

	VK_NULL_PTR( vkCreateShaderModule );
	VK_NULL_PTR( vkDestroyShaderModule );

	VK_NULL_PTR( vkCreatePipelineCache );
	VK_NULL_PTR( vkDestroyPipelineCache );
	VK_NULL_PTR( vkCreateGraphicsPipelines );
	VK_NULL_PTR( vkDestroyPipeline );

	VK_NULL_PTR( vkCreatePipelineLayout );
	VK_NULL_PTR( vkDestroyPipelineLayout );
	VK_NULL_PTR( vkCreateDescriptorSetLayout );
	VK_NULL_PTR( vkDestroyDescriptorSetLayout );
	VK_NULL_PTR( vkAllocateDescriptorSets );
	VK_NULL_PTR( vkFreeDescriptorSets );
	VK_NULL_PTR( vkUpdateDescriptorSets );

	VK_NULL_PTR( vkCreateRenderPass );
	VK_NULL_PTR( vkDestroyRenderPass );
	VK_NULL_PTR( vkCreateFramebuffer );
	VK_NULL_PTR( vkDestroyFramebuffer );

	VK_NULL_PTR( vkCmdBeginRenderPass );
	VK_NULL_PTR( vkCmdEndRenderPass );
	VK_NULL_PTR( vkCmdBindPipeline );
	VK_NULL_PTR( vkCmdBindVertexBuffers );
	VK_NULL_PTR( vkCmdBindIndexBuffer );
	VK_NULL_PTR( vkCmdBindDescriptorSets );
	VK_NULL_PTR( vkCmdDraw );
	VK_NULL_PTR( vkCmdDrawIndexed );
	VK_NULL_PTR( vkCmdClearAttachments );
	VK_NULL_PTR( vkCmdClearColorImage );
	VK_NULL_PTR( vkCmdClearDepthStencilImage );
	VK_NULL_PTR( vkCmdPipelineBarrier );
	VK_NULL_PTR( vkCmdSetViewport );
	VK_NULL_PTR( vkCmdSetScissor );
	VK_NULL_PTR( vkCmdSetBlendConstants );
	VK_NULL_PTR( vkCmdSetStencilReference );
	VK_NULL_PTR( vkCmdSetDepthBias );
	VK_NULL_PTR( vkCmdPushConstants );
	VK_NULL_PTR( vkCmdCopyBuffer );
	VK_NULL_PTR( vkCmdCopyImage );
	VK_NULL_PTR( vkCmdBlitImage );
	VK_NULL_PTR( vkCmdCopyBufferToImage );
	VK_NULL_PTR( vkCmdCopyImageToBuffer );

	VK_NULL_PTR( vkCreateFence );
	VK_NULL_PTR( vkDestroyFence );
	VK_NULL_PTR( vkResetFences );
	VK_NULL_PTR( vkWaitForFences );
	VK_NULL_PTR( vkGetFenceStatus );
	VK_NULL_PTR( vkCreateSemaphore );
	VK_NULL_PTR( vkDestroySemaphore );

	VK_NULL_PTR( vkCreateQueryPool );
	VK_NULL_PTR( vkDestroyQueryPool );
	VK_NULL_PTR( vkCmdBeginQuery );
	VK_NULL_PTR( vkCmdEndQuery );
	VK_NULL_PTR( vkCmdResetQueryPool );
	VK_NULL_PTR( vkGetQueryPoolResults );

	VK_NULL_PTR( vkCreateSwapchainKHR );
	VK_NULL_PTR( vkDestroySwapchainKHR );
	VK_NULL_PTR( vkGetSwapchainImagesKHR );
	VK_NULL_PTR( vkAcquireNextImageKHR );
	VK_NULL_PTR( vkQueuePresentKHR );

	VK_NULL_PTR( vkDestroySurfaceKHR );
	VK_NULL_PTR( vkGetPhysicalDeviceSurfaceSupportKHR );
	VK_NULL_PTR( vkGetPhysicalDeviceSurfaceCapabilitiesKHR );
	VK_NULL_PTR( vkGetPhysicalDeviceSurfaceFormatsKHR );
	VK_NULL_PTR( vkGetPhysicalDeviceSurfacePresentModesKHR );

#undef VK_NULL_PTR
}

CVulkanEntryPoints::~CVulkanEntryPoints()
{
	Shutdown();
}

//-----------------------------------------------------------------------------
// Initialize: libvulkan already loaded by VKConnectLibraries(); resolve all
// instance-level entry points, create the VkInstance / VkDevice, and load the
// device-level + swapchain entry points.
//-----------------------------------------------------------------------------
bool CVulkanEntryPoints::Initialize()
{
	if ( !s_vkGetInstanceProcAddr )
	{
		Warning( "CVulkanEntryPoints::Initialize: vkGetInstanceProcAddr not loaded (call VKConnectLibraries first)\n" );
		return false;
	}

	if ( !LoadInstanceFunctions() )
	{
		Warning( "CVulkanEntryPoints::Initialize: LoadInstanceFunctions failed\n" );
		return false;
	}

	// Enumerate physical devices and pick the first one that exposes a graphics queue.
	uint32_t gpuCount = 0;
	if ( vkEnumeratePhysicalDevices( m_instance, &gpuCount, NULL ) != VK_SUCCESS || gpuCount == 0 )
	{
		Warning( "CVulkanEntryPoints::Initialize: no Vulkan physical devices found\n" );
		return false;
	}

	CUtlVector<VkPhysicalDevice> physicalDevices;
	physicalDevices.SetCount( gpuCount );
	if ( vkEnumeratePhysicalDevices( m_instance, &gpuCount, physicalDevices.Base() ) != VK_SUCCESS )
	{
		Warning( "CVulkanEntryPoints::Initialize: vkEnumeratePhysicalDevices fetch failed\n" );
		return false;
	}

	// Select the first device exposing a graphics queue family.
	uint32_t selectedQueueFamily = ~0u;
	for ( uint32 i = 0; i < gpuCount; ++i )
	{
		VkPhysicalDevice physicalDevice = physicalDevices[i];
		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &queueFamilyCount, NULL );
		if ( queueFamilyCount == 0 )
			continue;

		CUtlVector<VkQueueFamilyProperties> queueFamilies;
		queueFamilies.SetCount( queueFamilyCount );
		vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &queueFamilyCount, queueFamilies.Base() );

		for ( uint32_t q = 0; q < queueFamilyCount; ++q )
		{
			if ( queueFamilies[q].queueFlags & VK_QUEUE_GRAPHICS_BIT )
			{
				m_physicalDevice = physicalDevice;
				selectedQueueFamily = q;
				break;
			}
		}
		if ( m_physicalDevice != VK_NULL_HANDLE )
			break;
	}

	if ( m_physicalDevice == VK_NULL_HANDLE || selectedQueueFamily == ~0u )
	{
		Warning( "CVulkanEntryPoints::Initialize: no physical device with a graphics queue\n" );
		return false;
	}

	// Record driver info for debugging / shader workarounds.
	VkPhysicalDeviceProperties physProps;
	memset( &physProps, 0, sizeof( physProps ) );
	vkGetPhysicalDeviceProperties( m_physicalDevice, &physProps );

	m_pDriverStrings[0] = physProps.deviceName;
	m_pDriverStrings[1] = physProps.deviceName;
	m_pDriverStrings[2] = physProps.deviceName;
	m_nApiVersionMajor = VK_VERSION_MAJOR( physProps.apiVersion );
	m_nApiVersionMinor = VK_VERSION_MINOR( physProps.apiVersion );
	m_nApiVersionPatch = VK_VERSION_PATCH( physProps.apiVersion );

	switch ( physProps.vendorID )
	{
		case VK_VENDOR_ID_NVIDIA:  m_nDriverProvider = cVKDriverProviderNVIDIA; break;
		case VK_VENDOR_ID_AMD:      m_nDriverProvider = cVKDriverProviderAMD; break;
		case VK_VENDOR_ID_INTEL:    m_nDriverProvider = cVKDriverProviderIntelOpenSource; break;
		case VK_VENDOR_ID_QUALCOMM: m_nDriverProvider = cVKDriverProviderQualcomm; break;
		case VK_VENDOR_ID_ARM:      m_nDriverProvider = cVKDriverProviderARM; break;
		case VK_VENDOR_ID_IMGTEC:   m_nDriverProvider = cVKDriverProviderImagination; break;
		default:                    m_nDriverProvider = cVKDriverProviderUnknown; break;
	}

	// Create the VkDevice with the graphics queue.
	float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo queueCreateInfo;
	memset( &queueCreateInfo, 0, sizeof( queueCreateInfo ) );
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = selectedQueueFamily;
	queueCreateInfo.queueCount = 1;
	queueCreateInfo.pQueuePriorities = &queuePriority;

	const char *ppDeviceExtensions[] =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	};

	VkPhysicalDeviceFeatures deviceFeatures;
	memset( &deviceFeatures, 0, sizeof( deviceFeatures ) );

	VkDeviceCreateInfo deviceCreateInfo;
	memset( &deviceCreateInfo, 0, sizeof( deviceCreateInfo ) );
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceCreateInfo.enabledExtensionCount = 1;
	deviceCreateInfo.ppEnabledExtensionNames = ppDeviceExtensions;
	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

	if ( vkCreateDevice( m_physicalDevice, &deviceCreateInfo, VK_NULL_HANDLE, &m_device ) != VK_SUCCESS )
	{
		Warning( "CVulkanEntryPoints::Initialize: vkCreateDevice failed\n" );
		return false;
	}

	// The graphics queue family index is re-derived in CVKContext::Create by
	// walking vkGetPhysicalDeviceQueueFamilyProperties again (the CVulkanEntryPoints
	// header intentionally does not expose m_queueFamilyIndex).

	if ( !LoadDeviceFunctions() )
	{
		Warning( "CVulkanEntryPoints::Initialize: LoadDeviceFunctions failed\n" );
		return false;
	}

	if ( !LoadSwapchainFunctions() )
	{
		Warning( "CVulkanEntryPoints::Initialize: LoadSwapchainFunctions failed\n" );
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Shutdown: tear down device + instance, close the shared library.
//-----------------------------------------------------------------------------
void CVulkanEntryPoints::Shutdown()
{
	if ( vkDestroyDevice && m_device != VK_NULL_HANDLE )
	{
		vkDeviceWaitIdle( m_device );
		vkDestroyDevice( m_device, VK_NULL_HANDLE );
		m_device = VK_NULL_HANDLE;
	}

	if ( vkDestroyInstance && m_instance != VK_NULL_HANDLE )
	{
		vkDestroyInstance( m_instance, VK_NULL_HANDLE );
		m_instance = VK_NULL_HANDLE;
	}

	m_physicalDevice = VK_NULL_HANDLE;

#if !defined( _WIN32 )
	if ( m_vulkanLib )
	{
		dlclose( m_vulkanLib );
		m_vulkanLib = NULL;
	}
#endif
	s_vkGetInstanceProcAddr = NULL;
}

//-----------------------------------------------------------------------------
// LoadInstanceFunctions: resolve everything reachable from a VkInstance.
//-----------------------------------------------------------------------------
bool CVulkanEntryPoints::LoadInstanceFunctions()
{
	// Core instance functions (resolved with NULL instance).
#define VK_LOAD( name ) \
	name = (PFN_##name) s_vkGetInstanceProcAddr( VK_NULL_HANDLE, #name );

	VK_LOAD( vkDestroyInstance );
	VK_LOAD( vkEnumeratePhysicalDevices );
	VK_LOAD( vkGetPhysicalDeviceProperties );
	VK_LOAD( vkGetPhysicalDeviceFeatures );
	VK_LOAD( vkGetPhysicalDeviceMemoryProperties );
	VK_LOAD( vkGetPhysicalDeviceQueueFamilyProperties );
	VK_LOAD( vkGetPhysicalDeviceFormatProperties );
	VK_LOAD( vkEnumerateDeviceExtensionProperties );
	VK_LOAD( vkCreateDevice );
#undef VK_LOAD

	if ( !vkDestroyInstance || !vkEnumeratePhysicalDevices || !vkCreateDevice )
	{
		Warning( "CVulkanEntryPoints::LoadInstanceFunctions: missing core instance entry points\n" );
		return false;
	}

	// Surface queries are also instance-level.
#define VK_LOAD_OPT( name ) \
	name = (PFN_##name) s_vkGetInstanceProcAddr( m_instance, #name );

	VK_LOAD_OPT( vkDestroySurfaceKHR );
	VK_LOAD_OPT( vkGetPhysicalDeviceSurfaceSupportKHR );
	VK_LOAD_OPT( vkGetPhysicalDeviceSurfaceCapabilitiesKHR );
	VK_LOAD_OPT( vkGetPhysicalDeviceSurfaceFormatsKHR );
	VK_LOAD_OPT( vkGetPhysicalDeviceSurfacePresentModesKHR );
#undef VK_LOAD_OPT

	return true;
}

//-----------------------------------------------------------------------------
// LoadDeviceFunctions: resolve device-level entry points via the VkDevice.
//-----------------------------------------------------------------------------
bool CVulkanEntryPoints::LoadDeviceFunctions()
{
#define VK_LOAD( name ) \
	name = (PFN_##name) vkGetDeviceProcAddr ? vkGetDeviceProcAddr( m_device, #name ) : s_vkGetInstanceProcAddr( m_instance, #name );

	// First, ensure vkGetDeviceProcAddr itself is available so we use the
	// fastest dispatch path for the rest.
	PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr) s_vkGetInstanceProcAddr( m_instance, "vkGetDeviceProcAddr" );

	VK_LOAD( vkDestroyDevice );
	VK_LOAD( vkGetDeviceQueue );
	VK_LOAD( vkDeviceWaitIdle );

	VK_LOAD( vkQueueSubmit );
	VK_LOAD( vkQueueWaitIdle );
	VK_LOAD( vkAllocateCommandBuffers );
	VK_LOAD( vkFreeCommandBuffers );
	VK_LOAD( vkBeginCommandBuffer );
	VK_LOAD( vkEndCommandBuffer );
	VK_LOAD( vkResetCommandBuffer );

	VK_LOAD( vkAllocateMemory );
	VK_LOAD( vkFreeMemory );
	VK_LOAD( vkMapMemory );
	VK_LOAD( vkUnmapMemory );
	VK_LOAD( vkFlushMappedMemoryRanges );
	VK_LOAD( vkInvalidateMappedMemoryRanges );
	VK_LOAD( vkBindBufferMemory );
	VK_LOAD( vkBindImageMemory );
	VK_LOAD( vkGetBufferMemoryRequirements );
	VK_LOAD( vkGetImageMemoryRequirements );
	VK_LOAD( vkGetImageSubresourceLayout );

	VK_LOAD( vkCreateBuffer );
	VK_LOAD( vkDestroyBuffer );

	VK_LOAD( vkCreateImage );
	VK_LOAD( vkDestroyImage );
	VK_LOAD( vkCreateImageView );
	VK_LOAD( vkDestroyImageView );

	VK_LOAD( vkCreateSampler );
	VK_LOAD( vkDestroySampler );

	VK_LOAD( vkCreateShaderModule );
	VK_LOAD( vkDestroyShaderModule );

	VK_LOAD( vkCreatePipelineCache );
	VK_LOAD( vkDestroyPipelineCache );
	VK_LOAD( vkCreateGraphicsPipelines );
	VK_LOAD( vkDestroyPipeline );

	VK_LOAD( vkCreatePipelineLayout );
	VK_LOAD( vkDestroyPipelineLayout );
	VK_LOAD( vkCreateDescriptorSetLayout );
	VK_LOAD( vkDestroyDescriptorSetLayout );
	VK_LOAD( vkAllocateDescriptorSets );
	VK_LOAD( vkFreeDescriptorSets );
	VK_LOAD( vkUpdateDescriptorSets );

	VK_LOAD( vkCreateRenderPass );
	VK_LOAD( vkDestroyRenderPass );
	VK_LOAD( vkCreateFramebuffer );
	VK_LOAD( vkDestroyFramebuffer );

	VK_LOAD( vkCmdBeginRenderPass );
	VK_LOAD( vkCmdEndRenderPass );
	VK_LOAD( vkCmdBindPipeline );
	VK_LOAD( vkCmdBindVertexBuffers );
	VK_LOAD( vkCmdBindIndexBuffer );
	VK_LOAD( vkCmdBindDescriptorSets );
	VK_LOAD( vkCmdDraw );
	VK_LOAD( vkCmdDrawIndexed );
	VK_LOAD( vkCmdClearAttachments );
	VK_LOAD( vkCmdClearColorImage );
	VK_LOAD( vkCmdClearDepthStencilImage );
	VK_LOAD( vkCmdPipelineBarrier );
	VK_LOAD( vkCmdSetViewport );
	VK_LOAD( vkCmdSetScissor );
	VK_LOAD( vkCmdSetBlendConstants );
	VK_LOAD( vkCmdSetStencilReference );
	VK_LOAD( vkCmdSetDepthBias );
	VK_LOAD( vkCmdPushConstants );
	VK_LOAD( vkCmdCopyBuffer );
	VK_LOAD( vkCmdCopyImage );
	VK_LOAD( vkCmdBlitImage );
	VK_LOAD( vkCmdCopyBufferToImage );
	VK_LOAD( vkCmdCopyImageToBuffer );

	VK_LOAD( vkCreateFence );
	VK_LOAD( vkDestroyFence );
	VK_LOAD( vkResetFences );
	VK_LOAD( vkWaitForFences );
	VK_LOAD( vkGetFenceStatus );
	VK_LOAD( vkCreateSemaphore );
	VK_LOAD( vkDestroySemaphore );

	VK_LOAD( vkCreateQueryPool );
	VK_LOAD( vkDestroyQueryPool );
	VK_LOAD( vkCmdBeginQuery );
	VK_LOAD( vkCmdEndQuery );
	VK_LOAD( vkCmdResetQueryPool );
	VK_LOAD( vkGetQueryPoolResults );
#undef VK_LOAD

	if ( !vkDestroyDevice || !vkGetDeviceQueue || !vkAllocateCommandBuffers || !vkAllocateMemory )
	{
		Warning( "CVulkanEntryPoints::LoadDeviceFunctions: missing core device entry points\n" );
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// LoadSwapchainFunctions: VK_KHR_swapchain entry points (instance-level).
//-----------------------------------------------------------------------------
bool CVulkanEntryPoints::LoadSwapchainFunctions()
{
#define VK_LOAD( name ) \
	name = (PFN_##name) s_vkGetInstanceProcAddr( m_instance, #name );

	VK_LOAD( vkCreateSwapchainKHR );
	VK_LOAD( vkDestroySwapchainKHR );
	VK_LOAD( vkGetSwapchainImagesKHR );
	VK_LOAD( vkAcquireNextImageKHR );
	VK_LOAD( vkQueuePresentKHR );
#undef VK_LOAD

	if ( !vkCreateSwapchainKHR || !vkDestroySwapchainKHR || !vkGetSwapchainImagesKHR ||
		 !vkAcquireNextImageKHR || !vkQueuePresentKHR )
	{
		Warning( "CVulkanEntryPoints::LoadSwapchainFunctions: missing swapchain entry points\n" );
		return false;
	}

	return true;
}

//=============================================================================
// VKConnectLibraries - mirror of ToGLConnectLibraries for the Vulkan backend.
// Loads libvulkan.so, resolves vkGetInstanceProcAddr, and creates the
// VkInstance with the surface + swapchain (and optionally debug_report)
// extensions enabled.
//=============================================================================
bool VKConnectLibraries()
{
#if !defined( _WIN32 )
	if ( !s_vulkanLibHandle )
	{
		void *libHandle = dlopen( "libvulkan.so", RTLD_NOW | RTLD_LOCAL );
#ifdef __ANDROID__
		if ( !libHandle )
		{
			libHandle = dlopen( "libvulkan.so", RTLD_NOW | RTLD_LOCAL );
		}
#endif
		if ( !libHandle )
		{
			Warning( "VKConnectLibraries: dlopen(\"libvulkan.so\") failed: %s\n", dlerror() );
			return false;
		}

		s_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) dlsym( libHandle, "vkGetInstanceProcAddr" );
		if ( !s_vkGetInstanceProcAddr )
		{
			Warning( "VKConnectLibraries: dlsym(vkGetInstanceProcAddr) failed: %s\n", dlerror() );
			dlclose( libHandle );
			return false;
		}

		s_vulkanLibHandle = libHandle;
	}
#else
	if ( !s_vkGetInstanceProcAddr )
	{
		s_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) VKLookupProc( "vkGetInstanceProcAddr", true );
		if ( !s_vkGetInstanceProcAddr )
		{
			Warning( "VKConnectLibraries: failed to resolve vkGetInstanceProcAddr\n" );
			return false;
		}
	}
#endif

	if ( !gVK )
	{
		gVK = new CVulkanEntryPoints();
	}

#ifdef USE_SDL
	// SDL_Vulkan_GetInstanceProcAddress can supply vkGetInstanceProcAddr too;
	// prefer it when available so we match the SDL loader path.
	PFN_vkGetInstanceProcAddr sdlProc =
		(PFN_vkGetInstanceProcAddr) SDL_Vulkan_GetInstanceProcAddress( VK_NULL_HANDLE, "vkGetInstanceProcAddr" );
	if ( sdlProc )
	{
		s_vkGetInstanceProcAddr = sdlProc;
	}
#endif

	// Gather the instance extensions we need.
	CUtlVector<const char *> instanceExtensions;

#if defined( __ANDROID__ )
	instanceExtensions.AddToTail( VK_KHR_SURFACE_EXTENSION_NAME );
	instanceExtensions.AddToTail( VK_KHR_ANDROID_SURFACE_EXTENSION_NAME );
#elif defined( USE_SDL )
	instanceExtensions.AddToTail( VK_KHR_SURFACE_EXTENSION_NAME );
	// SDL provides the platform surface extension; let it enumerate.
	unsigned int sdlExtCount = 0;
	if ( SDL_Vulkan_GetInstanceExtensions( NULL, &sdlExtCount, NULL ) )
	{
		CUtlVector<const char *> sdlExts;
		sdlExts.SetCount( sdlExtCount );
		if ( SDL_Vulkan_GetInstanceExtensions( NULL, &sdlExtCount, sdlExts.Base() ) )
		{
			for ( unsigned int i = 0; i < sdlExtCount; ++i )
			{
				instanceExtensions.AddToTail( sdlExts[i] );
			}
		}
	}
#elif !defined( _WIN32 )
	instanceExtensions.AddToTail( VK_KHR_SURFACE_EXTENSION_NAME );
	instanceExtensions.AddToTail( VK_KHR_XLIB_SURFACE_EXTENSION_NAME );
#endif

#ifdef _DEBUG
	instanceExtensions.AddToTail( VK_EXT_DEBUG_REPORT_EXTENSION_NAME );
#endif

	// Create the VkInstance.
	VkApplicationInfo appInfo;
	memset( &appInfo, 0, sizeof( appInfo ) );
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Source Engine";
	appInfo.applicationVersion = 0;
	appInfo.pEngineName = "TOGL-VK";
	appInfo.engineVersion = 0;
	appInfo.apiVersion = VULKAN_API_VERSION;

	VkInstanceCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = instanceExtensions.Count();
	createInfo.ppEnabledExtensionNames = instanceExtensions.Base();

#ifdef _DEBUG
	const char *ppLayers[] = { "VK_LAYER_LUNARG_standard_validation" };
	createInfo.enabledLayerCount = 1;
	createInfo.ppEnabledLayerNames = ppLayers;
#endif

	VkInstance instance = VK_NULL_HANDLE;
	VkResult res = s_vkGetInstanceProcAddr( VK_NULL_HANDLE, "vkCreateInstance" )
		? ( (PFN_vkCreateInstance) s_vkGetInstanceProcAddr( VK_NULL_HANDLE, "vkCreateInstance" ) )( &createInfo, VK_NULL_HANDLE, &instance )
		: VK_ERROR_INITIALIZATION_FAILED;

	if ( res != VK_SUCCESS )
	{
		Warning( "VKConnectLibraries: vkCreateInstance failed (VkResult=%d)\n", (int)res );
		return false;
	}

	gVK->m_instance = instance;

	// Now that the instance exists, load all entry points and pick a device.
	if ( !gVK->Initialize() )
	{
		Warning( "VKConnectLibraries: CVulkanEntryPoints::Initialize failed\n" );
		return false;
	}

	return true;
}

//=============================================================================
// Surface creation helpers - dispatched by platform.
//=============================================================================
#if defined( __ANDROID__ )
// Implemented in vkcontext.cpp where the platform window handle lives.
#elif !defined( _WIN32 ) && !defined( USE_SDL )
// Xlib surface creation is performed from vkcontext.cpp using the window handle.
#elif defined( USE_SDL )
// SDL_Vulkan_CreateSurface is invoked from vkcontext.cpp.
#endif
